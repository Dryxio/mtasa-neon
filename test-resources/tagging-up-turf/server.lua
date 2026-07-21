local mission = {
    running = false,
    finishing = false,
    stage = nil,
    leader = nil,
    party = {},
    snapshots = {},
    entities = {},
    tagProgress = {},
    completedTags = {},
    timers = {},
    demoLeaveSerial = 0,
    demoLeave = nil,
    demoWalkSerial = 0,
    demoWalk = nil,
    demoShootSerial = 0,
    demoShoot = nil,
    demoSceneSerial = 0,
    demoScene = nil,
    demoEnterSerial = 0,
    demoEnter = nil,
    ballasDepartureSerial = 0,
    ballasDeparture = nil,
    ballasGangSceneSerial = 0,
    ballasGangScene = nil,
    ballasGangSceneCompleted = false,
    ballasEncounterSerial = 0,
    ballasEncounter = nil,
    ballasTimerResetAt = nil,
    checkpointGroundSerial = 0,
    checkpointGroundPending = {},
    vehiclePlaybackSerial = 0,
    vehiclePlayback = nil,
    postRoofSceneSerial = 0,
    postRoofScene = nil,
    introSceneSerial = 0,
    introScene = nil,
    introEntryPending = false,
    introEntryGuardTimer = nil,
    fileCutsceneSerial = 0,
    fileCutscene = nil,
    finalSceneSerial = 0,
    finalScene = nil,
    transitionAudioSerial = 0,
    transitionAudio = nil,
    reminderIndex = 1,
    offscreenStored = false,
    vehiclePlayerOnlyLocked = false,
}

-- GTA produces every spray hit and exact alpha step. The server validates those
-- native reports and mirrors the resulting byte so co-op clients share one state.

local function isMissionPlayer(player)
    if not isElement(player) then
        return false
    end
    for _, member in ipairs(mission.party) do
        if member == player then
            return true
        end
    end
    return false
end

local function rememberTimer(timer)
    table.insert(mission.timers, timer)
    return timer
end

local function clearMissionTimers()
    for _, timer in ipairs(mission.timers) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    mission.timers = {}
end

local function snapshotPedClothes(ped)
    local clothes = {}
    for clothingType = 0, TAGUP.cj.clothingSlots - 1 do
        local texture, model = getPedClothes(ped, clothingType)
        if type(texture) == "string" and type(model) == "string" then
            clothes[clothingType] = {texture = texture, model = model}
        end
    end
    return clothes
end

local function clearPedClothes(ped)
    for clothingType = 0, TAGUP.cj.clothingSlots - 1 do
        local texture = getPedClothes(ped, clothingType)
        if type(texture) == "string" and not removePedClothes(ped, clothingType) then
            return false, clothingType
        end
    end
    return true
end

local function applyPedClothes(ped, clothes)
    local cleared, failedType = clearPedClothes(ped)
    if not cleared then
        return false, ("remove slot %d refused"):format(failedType)
    end
    for clothingType, clothing in pairs(clothes or {}) do
        if not addPedClothes(ped, clothing.texture, clothing.model, clothingType) then
            return false, ("add slot %d %s/%s refused"):format(clothingType, clothing.texture, clothing.model)
        end
    end
    return true
end

local function applyMissionCJ(player)
    if not isElement(player) then
        return false, "leader element unavailable"
    end
    -- MTA reports false when setElementModel is a no-op. A player who already
    -- uses CJ model 0 is valid and still needs the mission clothing profile.
    if getElementModel(player) ~= TAGUP.cj.model and not setElementModel(player, TAGUP.cj.model) then
        return false, "CJ model 0 refused"
    end
    local clothes = {}
    for _, clothing in ipairs(TAGUP.cj.clothes) do
        clothes[clothing.type] = {texture = clothing.texture, model = clothing.model}
    end
    local applied, details = applyPedClothes(player, clothes)
    if not applied then
        return false, details
    end
    for _, expected in ipairs(TAGUP.cj.clothes) do
        local texture, model = getPedClothes(player, expected.type)
        if type(texture) ~= "string" or type(model) ~= "string" or texture:lower() ~= expected.texture or
            model:lower() ~= expected.model then
            return false, ("slot %d readback mismatch: %s/%s"):format(expected.type, tostring(texture), tostring(model))
        end
    end
    outputDebugString(("[tagging-up-turf] Vanilla CJ appearance applied to %s: model=0 vest/player_face/jeansdenim/sneakerbincblk"):format(
                          getPlayerName(player)))
    return true
end

local function snapshotPlayer(player)
    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local weapons = {}
    for slot = 0, 12 do
        local weapon = getPedWeapon(player, slot)
        local ammo = getPedTotalAmmo(player, slot)
        if weapon and weapon ~= 0 and ammo > 0 then
            table.insert(weapons, {weapon = weapon, ammo = ammo})
        end
    end
    return {
        x = x,
        y = y,
        z = z,
        rotation = rotation,
        interior = getElementInterior(player),
        dimension = getElementDimension(player),
        health = getElementHealth(player),
        armor = getPedArmor(player),
        model = getElementModel(player),
        clothes = snapshotPedClothes(player),
        weapons = weapons,
    }
end

local function restorePlayerAppearance(player, snapshot)
    if not isElement(player) or not snapshot or not snapshot.cjAppearanceApplied then
        return true
    end

    -- Clothes can only be changed while the element uses CJ model 0. Rebuild
    -- that hidden state first, then return to the player's original skin.
    if getElementModel(player) ~= TAGUP.cj.model and not setElementModel(player, TAGUP.cj.model) then
        return false
    end
    local restored, details = applyPedClothes(player, snapshot.clothes)
    if not restored then
        outputDebugString(("[tagging-up-turf] Failed to restore clothes for %s: %s"):format(
                              getPlayerName(player), tostring(details)), 2)
    end
    if getElementModel(player) ~= snapshot.model then
        setElementModel(player, snapshot.model)
    end
    outputDebugString(("[tagging-up-turf] Restored appearance for %s: model=%d clothes=%s"):format(
                          getPlayerName(player), snapshot.model, restored and "restored" or "failed"),
                      restored and 3 or 2)
    return restored
end

local function restorePlayer(player, snapshot)
    if not isElement(player) or not snapshot then
        return
    end

    local restoreModel = snapshot.cjAppearanceApplied and TAGUP.cj.model or snapshot.model
    if isPedDead(player) then
        spawnPlayer(player, snapshot.x, snapshot.y, snapshot.z, snapshot.rotation, restoreModel, snapshot.interior, snapshot.dimension)
    else
        removePedFromVehicle(player)
        setElementInterior(player, snapshot.interior)
        setElementDimension(player, snapshot.dimension)
        setElementPosition(player, snapshot.x, snapshot.y, snapshot.z)
        setElementRotation(player, 0, 0, snapshot.rotation)
        setElementModel(player, restoreModel)
    end

    restorePlayerAppearance(player, snapshot)

    setElementFrozen(player, false)
    setElementHealth(player, math.max(1, snapshot.health))
    setPedArmor(player, snapshot.armor)
    takeAllWeapons(player)
    for _, weapon in ipairs(snapshot.weapons) do
        giveWeapon(player, weapon.weapon, weapon.ammo, false)
    end
end

local function destroyMissionEntities()
    for _, entity in pairs(mission.entities) do
        if isElement(entity) then
            destroyElement(entity)
        end
    end
    mission.entities = {}
end

local function warpSweetIntoFirstFreeSeat()
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) then
        return false
    end

    if getPedOccupiedVehicle(sweet) == vehicle then
        return true
    end

    removePedFromVehicle(sweet)
    setElementDimension(sweet, TAGUP.dimension)
    local x, y, z = getElementPosition(vehicle)
    setElementPosition(sweet, x, y, z + 1)
    if isElement(mission.leader) then
        setElementSyncer(sweet, mission.leader)
    end

    for seat = 1, getVehicleMaxPassengers(vehicle) do
        if not getVehicleOccupant(vehicle, seat) then
            warpPedIntoVehicle(sweet, vehicle, seat)
            if getPedOccupiedVehicle(sweet) == vehicle then
                outputDebugString("[tagging-up-turf] Sweet seated in passenger seat " .. seat)
                return true
            end
        end
    end
    outputDebugString("[tagging-up-turf] Unable to seat Sweet: no usable passenger seat", 1)
    return false
end

local function stagePayload(extra)
    local payload = {
        stage = mission.stage,
        vehicle = mission.entities.vehicle,
        sweet = mission.entities.sweet,
        demoTag = mission.entities.demoTag,
        leader = mission.leader,
        tagProgress = mission.tagProgress,
        completedTags = mission.completedTags,
        vehiclePlayerOnlyLocked = mission.vehiclePlayerOnlyLocked,
    }
    if extra then
        for key, value in pairs(extra) do
            payload[key] = value
        end
    end
    return payload
end

local function broadcastState(extra)
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:state", resourceRoot, stagePayload(extra))
        end
    end
end

local function setStage(stage, extra)
    mission.stage = stage
    outputDebugString("[tagging-up-turf] Stage: " .. stage)
    broadcastState(extra)
end

local function cancelDemoLeave(reason)
    local leave = mission.demoLeave
    if not leave then
        return
    end

    mission.demoLeave = nil
    if isTimer(leave.guardTimer) then
        killTimer(leave.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoLeaveCancel", resourceRoot, leave.id, reason or "server_cancelled")
    end
end

local function cancelDemoWalk(reason)
    local walk = mission.demoWalk
    if not walk then
        return
    end

    mission.demoWalk = nil
    if isTimer(walk.guardTimer) then
        killTimer(walk.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoWalkCancel", resourceRoot, walk.id, reason or "server_cancelled")
    end
end

local function cancelDemoShoot(reason)
    local shoot = mission.demoShoot
    if not shoot then
        return
    end

    mission.demoShoot = nil
    if isTimer(shoot.guardTimer) then
        killTimer(shoot.guardTimer)
    end
    if isTimer(shoot.progressTimer) then
        killTimer(shoot.progressTimer)
    end
    if isTimer(shoot.completionTimer) then
        killTimer(shoot.completionTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetDemoSequenceCancel", resourceRoot, shoot.id, reason or "server_cancelled")
    end
end

local function cancelDemoEnter(reason)
    local enter = mission.demoEnter
    if not enter then
        return
    end

    mission.demoEnter = nil
    if isTimer(enter.guardTimer) then
        killTimer(enter.guardTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:sweetReturnEnterCancel", resourceRoot, enter.id, reason or "server_cancelled")
    end
end

local function cancelDemoScene(reason)
    local scene = mission.demoScene
    if not scene then
        return
    end

    mission.demoScene = nil
    for _, timerName in ipairs({"readyGuardTimer", "playerExitTimer", "fadeTimer", "stageTimer", "skipArmTimer", "audioGuardTimer", "animationGuardTimer",
                                "finalCheckGuardTimer", "releaseGuardTimer"}) do
        if isTimer(scene[timerName]) then
            killTimer(scene[timerName])
        end
    end
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoSceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
        end
    end
end

local function cancelBallasDeparture(reason)
    local departure = mission.ballasDeparture
    if not departure then
        return
    end

    mission.ballasDeparture = nil
    if isTimer(departure.guardTimer) then
        killTimer(departure.guardTimer)
    end
    if isTimer(departure.cameraGuardTimer) then
        killTimer(departure.cameraGuardTimer)
    end
    if isTimer(departure.finalCheckGuardTimer) then
        killTimer(departure.finalCheckGuardTimer)
    end
    if isTimer(departure.postStartTimer) then
        killTimer(departure.postStartTimer)
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasDepartureCancel", resourceRoot, departure.id, reason or "server_cancelled")
        end
    end
end

local function cancelBallasGangScene(reason)
    local scene = mission.ballasGangScene
    if not scene then
        return
    end

    mission.ballasGangScene = nil
    if isTimer(scene.readyGuardTimer) then
        killTimer(scene.readyGuardTimer)
    end
    if isTimer(scene.preSkipTimer) then
        killTimer(scene.preSkipTimer)
    end
    if isTimer(scene.completionTimer) then
        killTimer(scene.completionTimer)
    end
    if isTimer(scene.finalCheckGuardTimer) then
        killTimer(scene.finalCheckGuardTimer)
    end
    if isTimer(scene.releaseGuardTimer) then
        killTimer(scene.releaseGuardTimer)
    end
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasGangSceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
        end
    end
end

local function cancelBallasEncounter(reason)
    local encounter = mission.ballasEncounter
    if not encounter then
        return
    end

    mission.ballasEncounter = nil
    if isTimer(encounter.attackTimer) then
        killTimer(encounter.attackTimer)
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasEncounterCancel", resourceRoot, encounter.id, reason or "server_cancelled")
        end
    end
end

local function cancelPostRoofScene(reason, notifyClients)
    local scene = mission.postRoofScene
    if not scene then
        return
    end

    mission.postRoofScene = nil
    for _, timer in ipairs({scene.guardTimer, scene.hornTimer, scene.releaseGuardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if notifyClients ~= false then
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:postRoofSceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
            end
        end
    end
end

local function cancelVehiclePlayback(reason)
    local playback = mission.vehiclePlayback
    if not playback then
        return
    end

    mission.vehiclePlayback = nil
    if isTimer(playback.guardTimer) then
        killTimer(playback.guardTimer)
    end
    if isTimer(playback.startTimer) then
        killTimer(playback.startTimer)
    end
    if isTimer(playback.completionTimer) then
        killTimer(playback.completionTimer)
    end
    if isTimer(playback.sceneStartTimer) then
        killTimer(playback.sceneStartTimer)
    end
    if isElement(mission.leader) then
        triggerClientEvent(mission.leader, "tagup:vehiclePlaybackCancel", resourceRoot, playback.id, reason or "server_cancelled")
    end
end

local function isPartyInVehicle()
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) then
        return false
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) and getPedOccupiedVehicle(player) ~= vehicle then
            return false
        end
    end
    return true
end

local function createTagObject(tag)
    local object = createObject(TAGUP.tagModel, tag.x, tag.y, tag.z, 0, 0, tag.rotation)
    if object then
        setElementDimension(object, TAGUP.dimension)
        setElementInterior(object, 0)
        setElementCollisionsEnabled(object, false)
        setElementData(object, "tagup.tagId", tag.id, true)
        -- GTA stores the rival and Grove artwork in two materials of this same
        -- model. The synchronized byte drives only the Grove material client-side.
        setElementData(object, "tagup.paintAlpha", 0, true)
        mission.entities["tag" .. tag.id] = object
    end
    return object
end

local function updateTagVisual(tagId, alpha)
    local tag = mission.entities["tag" .. tagId]
    if isElement(tag) then
        setElementData(tag, "tagup.paintAlpha", math.max(0, math.min(255, math.floor(alpha + 0.5))), true)
    end
end

local function replaceTagObject(tagId)
    local tag = mission.entities["tag" .. tagId]
    if isElement(tag) then
        setElementData(tag, "tagup.paintAlpha", 255, true)
    end
end

local failMission
local finishMission
local createScmChar
local createMissionEntities
local startIntroScene
local startFinalScene

local function allTransitionAudioPlayers(audio, field)
    for _, player in ipairs(audio.players) do
        if isElement(player) and not audio[field][player] then
            return false
        end
    end
    return true
end

local function cancelTransitionAudio(reason)
    local audio = mission.transitionAudio
    if not audio then
        return
    end
    mission.transitionAudio = nil
    if isTimer(audio.guardTimer) then
        killTimer(audio.guardTimer)
    end
    for _, player in ipairs(audio.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:transitionAudioCancel", resourceRoot, audio.id, reason or "server_cancelled")
        end
    end
end

local function startTransitionAudio(profile, purpose, onComplete)
    if mission.transitionAudio or type(profile) ~= "table" then
        return false
    end

    mission.transitionAudioSerial = mission.transitionAudioSerial + 1
    local audio = {
        id = mission.transitionAudioSerial,
        purpose = purpose,
        profile = profile,
        players = {},
        readyPlayers = {},
        finishedPlayers = {},
        onComplete = onComplete,
        requestedAt = getTickCount(),
    }
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            table.insert(audio.players, player)
        end
    end
    mission.transitionAudio = audio
    audio.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.transitionAudio
        if active and active.id == expectedId then
            cancelTransitionAudio("server_timeout")
            failMission(("La replique %s a depasse son delai de garde."):format(active.profile.key))
        end
    end, TAGUP.transitionAudio.loadTimeout + TAGUP.transitionAudio.finishTimeout, 1, audio.id))

    outputDebugString(("[tagging-up-turf] Preparing transition audio #%d purpose=%s key=%s event=%d for %d participant(s)"):format(
                          audio.id, purpose, profile.key, profile.event, #audio.players))
    for _, player in ipairs(audio.players) do
        triggerClientEvent(player, "tagup:transitionAudioPrepare", resourceRoot, audio.id, purpose, profile)
    end
    return true
end

addEvent("tagup:transitionAudioReady", true)
addEventHandler("tagup:transitionAudioReady", resourceRoot, function(audioId, result, details)
    local player, audio = client, mission.transitionAudio
    if source ~= resourceRoot or not audio or audio.id ~= tonumber(audioId) or not isMissionPlayer(player) or audio.readyPlayers[player] then
        return
    end
    if result ~= "ready" then
        cancelTransitionAudio("client_prepare_" .. tostring(result))
        return failMission(("La replique %s n'a pas charge: %s"):format(audio.profile.key, tostring(details or result)))
    end
    audio.readyPlayers[player] = true
    if allTransitionAudioPlayers(audio, "readyPlayers") then
        audio.startedAt = getTickCount()
        for _, member in ipairs(audio.players) do
            if isElement(member) then
                triggerClientEvent(member, "tagup:transitionAudioStart", resourceRoot, audio.id)
            end
        end
    end
end)

addEvent("tagup:transitionAudioResult", true)
addEventHandler("tagup:transitionAudioResult", resourceRoot, function(audioId, result, details)
    local player, audio = client, mission.transitionAudio
    if source ~= resourceRoot or not audio or audio.id ~= tonumber(audioId) or not audio.startedAt or not isMissionPlayer(player) or
        audio.finishedPlayers[player] then
        return
    end
    if result ~= "finished" then
        cancelTransitionAudio("client_play_" .. tostring(result))
        return failMission(("La replique %s a echoue: %s"):format(audio.profile.key, tostring(details or result)))
    end
    audio.finishedPlayers[player] = true
    if allTransitionAudioPlayers(audio, "finishedPlayers") then
        local callback = audio.onComplete
        outputDebugString(("[tagging-up-turf] Transition audio #%d key=%s finished naturally on every participant after %d ms"):format(
                              audio.id, audio.profile.key, getTickCount() - audio.startedAt))
        cancelTransitionAudio("completed")
        if callback then
            callback()
        end
    end
end)

local function cancelFileCutscene(reason, notifyClients)
    local scene = mission.fileCutscene
    if not scene then
        return
    end

    mission.fileCutscene = nil
    for _, timer in ipairs({scene.loadGuardTimer, scene.finishGuardTimer, scene.releaseGuardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if notifyClients ~= false then
        for _, player in ipairs(scene.players) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:fileCutsceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
            end
        end
    end
end

local function allFileCutscenePlayers(scene, field)
    for _, player in ipairs(scene.players) do
        if isElement(player) and not scene[field][player] then
            return false
        end
    end
    return true
end

local function failFileCutscene(scene, reason)
    if mission.fileCutscene ~= scene then
        return
    end
    cancelFileCutscene("failed", true)
    failMission(reason)
end

local function clearIntroEntryGuard()
    if isTimer(mission.introEntryGuardTimer) then
        killTimer(mission.introEntryGuardTimer)
    end
    mission.introEntryGuardTimer = nil
end

local function cancelIntroScene(reason, notifyClients)
    local scene = mission.introScene
    if not scene then
        return
    end

    mission.introScene = nil
    for _, timer in ipairs({scene.readyGuardTimer, scene.startTimer, scene.audioGuardTimer, scene.nextLineTimer,
                            scene.releaseTimer, scene.entryReportGuardTimer, scene.releaseGuardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if notifyClients ~= false then
        for _, player in ipairs(scene.players) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:introSceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
            end
        end
    end
    local smoke = mission.entities.smoke
    if isElement(smoke) then
        destroyElement(smoke)
    end
    mission.entities.smoke = nil
    mission.introEntryPending = false
    clearIntroEntryGuard()
end

local function allIntroScenePlayers(scene, field)
    for _, player in ipairs(scene.players) do
        if isElement(player) and not scene[field][player] then
            return false
        end
    end
    return true
end

local function failIntroScene(scene, reason)
    if mission.introScene ~= scene then
        return
    end
    cancelIntroScene("failed", true)
    failMission(reason)
end

local requestIntroSceneRelease
local prepareIntroSceneLine

local function playIntroSceneLine(scene, lineIndex)
    if mission.introScene ~= scene or scene.lineIndex ~= lineIndex or scene.audioStarted then
        return
    end
    scene.audioStarted = true
    scene.audioFinishedPlayers = {}
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
    end
    scene.audioGuardTimer = rememberTimer(setTimer(function(expectedId, expectedLine)
        local active = mission.introScene
        if active and active.id == expectedId and active.lineIndex == expectedLine then
            failIntroScene(active, ("La replique d'intro %d a depasse son delai de garde."):format(expectedLine))
        end
    end, TAGUP.introScene.audioFinishTimeout, 1, scene.id, lineIndex))

    if lineIndex == 3 and isElement(mission.entities.sweet) then
        -- SWEET1 starts IDLE_CHAT with a 6000 ms lifetime during SWE1_AC.
        setPedAnimation(mission.entities.sweet, "PED", "IDLE_CHAT", 6000, true, false, false, false, 250, false)
    end
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:introScenePlayAudio", resourceRoot, scene.id, lineIndex, player == mission.leader)
        end
    end
    outputDebugString(("[tagging-up-turf] Intro world scene #%d playing %s"):format(
                          scene.id, TAGUP.introScene.audio[lineIndex].key))
end

prepareIntroSceneLine = function(scene, lineIndex)
    if mission.introScene ~= scene or not TAGUP.introScene.audio[lineIndex] then
        return
    end
    scene.lineIndex = lineIndex
    scene.audioStarted = false
    scene.audioReadyPlayers = {}
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
    end
    scene.audioGuardTimer = rememberTimer(setTimer(function(expectedId, expectedLine)
        local active = mission.introScene
        if active and active.id == expectedId and active.lineIndex == expectedLine then
            failIntroScene(active, ("Le chargement de la replique d'intro %d a depasse son delai de garde."):format(expectedLine))
        end
    end, TAGUP.introScene.audioLoadTimeout, 1, scene.id, lineIndex))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:introScenePrepareAudio", resourceRoot, scene.id, lineIndex)
        end
    end
end

requestIntroSceneRelease = function(scene)
    if mission.introScene ~= scene or scene.releasing then
        return
    end
    scene.releasing = true
    scene.releasedPlayers = {}
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.introScene
        if active and active.id == expectedId then
            failIntroScene(active, "La camera d'intro n'a pas ete restauree sur tous les clients.")
        end
    end, TAGUP.introScene.releaseTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:introSceneRelease", resourceRoot, scene.id)
        end
    end
end

local function startFileCutscene()
    mission.fileCutsceneSerial = mission.fileCutsceneSerial + 1
    local scene = {
        id = mission.fileCutsceneSerial,
        players = {},
        readyPlayers = {},
        startedPlayers = {},
        finishedPlayers = {},
        releasedPlayers = {},
    }
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            table.insert(scene.players, player)
        end
    end
    if #scene.players == 0 then
        return failMission("Aucun participant n'est disponible pour SWEET1A.")
    end

    mission.fileCutscene = scene
    scene.loadGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.fileCutscene
        if active and active.id == expectedId then
            failFileCutscene(active, "Le chargement natif de SWEET1A a depasse son delai de garde.")
        end
    end, TAGUP.fileCutscene.loadTimeout, 1, scene.id))

    setStage("sweet1a")
    for _, player in ipairs(scene.players) do
        triggerClientEvent(player, "tagup:fileCutscenePrepare", resourceRoot, scene.id, player == mission.leader)
    end
    outputDebugString(("[tagging-up-turf] SWEET1A file cutscene #%d loading for %d participant(s)"):format(scene.id, #scene.players))
end

addEvent("tagup:fileCutsceneReady", true)
addEventHandler("tagup:fileCutsceneReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.fileCutscene
    if source ~= resourceRoot or not scene or mission.stage ~= "sweet1a" or scene.id ~= tonumber(sceneId) or
        not isMissionPlayer(player) or scene.readyPlayers[player] then
        return
    end
    outputDebugString(("[tagging-up-turf] SWEET1A #%d loaded on %s result=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        return failFileCutscene(scene, "SWEET1A n'a pas pu etre chargee sur un client: " .. tostring(result))
    end
    scene.readyPlayers[player] = true
    if not allFileCutscenePlayers(scene, "readyPlayers") then
        return
    end

    if isTimer(scene.loadGuardTimer) then
        killTimer(scene.loadGuardTimer)
        scene.loadGuardTimer = nil
    end
    scene.finishGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.fileCutscene
        if active and active.id == expectedId then
            failFileCutscene(active, "La lecture native de SWEET1A a depasse son delai de garde.")
        end
    end, TAGUP.fileCutscene.finishTimeout, 1, scene.id))
    for _, member in ipairs(scene.players) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:fileCutsceneStart", resourceRoot, scene.id)
        end
    end
end)

addEvent("tagup:fileCutsceneStarted", true)
addEventHandler("tagup:fileCutsceneStarted", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.fileCutscene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or scene.startedPlayers[player] then
        return
    end
    if result ~= "started" then
        return failFileCutscene(scene, "Le demarrage natif de SWEET1A a echoue sur un client: " .. tostring(result))
    end
    scene.startedPlayers[player] = true
    if allFileCutscenePlayers(scene, "startedPlayers") then
        outputDebugString(("[tagging-up-turf] SWEET1A #%d started on every participant"):format(scene.id))
    end
end)

addEvent("tagup:fileCutsceneSkipRequest", true)
addEventHandler("tagup:fileCutsceneSkipRequest", resourceRoot, function(sceneId)
    local scene = mission.fileCutscene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.skipRequested then
        return
    end
    scene.skipRequested = true
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:fileCutsceneSkip", resourceRoot, scene.id)
        end
    end
    outputDebugString(("[tagging-up-turf] SWEET1A #%d native skip authorized by leader"):format(scene.id))
end)

addEvent("tagup:fileCutsceneFinished", true)
addEventHandler("tagup:fileCutsceneFinished", resourceRoot, function(sceneId, result, skipped, elapsed)
    local player, scene = client, mission.fileCutscene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or scene.finishedPlayers[player] then
        return
    end
    if result ~= "finished" then
        return failFileCutscene(scene, "La lecture native de SWEET1A a echoue sur un client: " .. tostring(result))
    end
    scene.finishedPlayers[player] = true
    outputDebugString(("[tagging-up-turf] SWEET1A #%d finished on %s skipped=%s elapsed=%s ms"):format(
                          scene.id, getPlayerName(player), tostring(skipped == true), tostring(elapsed or "?")))
    if not allFileCutscenePlayers(scene, "finishedPlayers") then
        return
    end

    if isTimer(scene.finishGuardTimer) then
        killTimer(scene.finishGuardTimer)
        scene.finishGuardTimer = nil
    end
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.fileCutscene
        if active and active.id == expectedId then
            failFileCutscene(active, "Le cleanup natif de SWEET1A a depasse son delai de garde.")
        end
    end, TAGUP.fileCutscene.releaseTimeout, 1, scene.id))
    for _, member in ipairs(scene.players) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:fileCutsceneRelease", resourceRoot, scene.id)
        end
    end
end)

addEvent("tagup:fileCutsceneReleased", true)
addEventHandler("tagup:fileCutsceneReleased", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.fileCutscene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or scene.releasedPlayers[player] then
        return
    end
    if result ~= "released" then
        return failFileCutscene(scene, "Le cleanup natif de SWEET1A a echoue sur un client.")
    end
    scene.releasedPlayers[player] = true
    if not allFileCutscenePlayers(scene, "releasedPlayers") then
        return
    end

    cancelFileCutscene("completed", false)
    for _, member in ipairs(mission.party) do
        if member ~= mission.leader and isElement(member) then
            restorePlayerAppearance(member, mission.snapshots[member])
            mission.snapshots[member].cjAppearanceApplied = false
        end
    end
    if not createMissionEntities(mission.leader) then
        return failMission("Les entites de mission n'ont pas pu etre creees apres SWEET1A.")
    end
    outputDebugString(("[tagging-up-turf] SWEET1A #%d cleared on every participant; starting world intro"):format(tonumber(sceneId)))
    startIntroScene()
end)

startIntroScene = function()
    local leader, sweet = mission.leader, mission.entities.sweet
    if not isElement(leader) or not isElement(sweet) then
        return failMission("Sweet ou le leader est indisponible pour la scene d'intro.")
    end
    local profile = TAGUP.introScene
    setElementPosition(leader, profile.leaderStart.x, profile.leaderStart.y, profile.leaderStart.z)
    setElementRotation(leader, 0, 0, profile.leaderStart.heading)
    -- CREATE_CHAR adds 1.0 to the script Z before native placement.
    setElementPosition(sweet, profile.sweetStart.x, profile.sweetStart.y, profile.sweetStart.z + 1.0)
    setElementRotation(sweet, 0, 0, profile.sweetStart.heading)
    setElementSyncer(sweet, leader, true, true)

    local smoke = createScmChar(profile.smoke.model, profile.smoke.start.x, profile.smoke.start.y, profile.smoke.start.z,
                                profile.smoke.start.heading)
    if not smoke then
        return failMission("Big Smoke n'a pas pu etre cree pour la scene d'intro.")
    end
    setElementDimension(smoke, TAGUP.dimension)
    setElementData(smoke, TAGUP.missionActorData, true, true)
    -- SWEET1 assigns Smoke's FATMAN motion group to this mission actor.
    setPedWalkingStyle(smoke, profile.smoke.walkingStyle)
    setElementSyncer(smoke, leader, true, true)
    mission.entities.smoke = smoke

    mission.introSceneSerial = mission.introSceneSerial + 1
    local scene = {
        id = mission.introSceneSerial,
        players = {},
        readyPlayers = {},
        lineIndex = 1,
        requestedAt = getTickCount(),
    }
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            table.insert(scene.players, player)
        end
    end
    mission.introScene = scene
    mission.introEntryPending = false
    scene.readyGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.introScene
        if active and active.id == expectedId then
            failIntroScene(active, "La preparation de la scene d'intro a depasse son delai de garde.")
        end
    end, profile.readyTimeout, 1, scene.id))

    setStage("intro")
    for _, player in ipairs(scene.players) do
        triggerClientEvent(player, "tagup:introScenePrepare", resourceRoot, scene.id, sweet, smoke)
    end
    outputDebugString(("[tagging-up-turf] Intro world scene #%d preparing fixed camera and SWE1_AA for %d participant(s)"):format(
                          scene.id, #scene.players))
end

addEvent("tagup:introSceneReady", true)
addEventHandler("tagup:introSceneReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.introScene
    if source ~= resourceRoot or not scene or mission.stage ~= "intro" or scene.id ~= tonumber(sceneId) or
        not isMissionPlayer(player) or scene.readyPlayers[player] or scene.started then
        return
    end
    outputDebugString(("[tagging-up-turf] Intro world scene #%d ready player=%s result=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        return failIntroScene(scene, "La preparation de la scene d'intro a echoue sur un client: " .. tostring(result))
    end
    scene.readyPlayers[player] = true
    if not allIntroScenePlayers(scene, "readyPlayers") then
        return
    end

    scene.started = true
    scene.startedAt = getTickCount()
    scene.audioReadyPlayers = scene.readyPlayers
    if isTimer(scene.readyGuardTimer) then
        killTimer(scene.readyGuardTimer)
        scene.readyGuardTimer = nil
    end
    setElementFrozen(mission.leader, false)
    for _, member in ipairs(scene.players) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:introSceneStart", resourceRoot, scene.id)
        end
    end
    scene.startTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.introScene
        if active and active.id == expectedId then
            failIntroScene(active, "Les tasks de marche natives de l'intro n'ont pas ete acceptees a temps.")
        end
    end, 3000, 1, scene.id))
end)

addEvent("tagup:introSceneTasksStarted", true)
addEventHandler("tagup:introSceneTasksStarted", resourceRoot, function(sceneId, smokeAccepted, sweetAccepted, leaderAccepted)
    local scene = mission.introScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.started or
        scene.tasksReported then
        return
    end
    scene.tasksReported = true
    if smokeAccepted ~= true or sweetAccepted ~= true or leaderAccepted ~= true then
        return failIntroScene(scene, ("Une task de marche native de l'intro a ete refusee (Smoke=%s Sweet=%s leader=%s)."):format(
                                  tostring(smokeAccepted), tostring(sweetAccepted), tostring(leaderAccepted)))
    end
    if isTimer(scene.startTimer) then
        killTimer(scene.startTimer)
        scene.startTimer = nil
    end
    local elapsed = getTickCount() - (scene.startedAt or getTickCount())
    local delay = math.max(50, math.floor(TAGUP.introScene.camera.fadeInDuration * 1000 + 0.5) - elapsed)
    scene.startTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.introScene
        if active and active.id == expectedId then
            playIntroSceneLine(active, 1)
        end
    end, delay, 1, scene.id))
end)

addEvent("tagup:introSceneAudioReady", true)
addEventHandler("tagup:introSceneAudioReady", resourceRoot, function(sceneId, lineIndex, result, details)
    local player, scene = client, mission.introScene
    lineIndex = tonumber(lineIndex)
    if source ~= resourceRoot or not scene or mission.stage ~= "intro" or scene.id ~= tonumber(sceneId) or
        scene.lineIndex ~= lineIndex or scene.audioStarted or not isMissionPlayer(player) or scene.audioReadyPlayers[player] then
        return
    end
    if result ~= "ready" then
        return failIntroScene(scene, ("Le chargement de la replique d'intro %d a echoue sur un client: %s"):format(
                                  lineIndex or -1, tostring(result)))
    end
    scene.audioReadyPlayers[player] = true
    outputDebugString(("[tagging-up-turf] Intro world scene #%d line=%d ready on %s (%s)"):format(
                          scene.id, lineIndex, getPlayerName(player), tostring(details or ""):sub(1, 180)))
    if allIntroScenePlayers(scene, "audioReadyPlayers") then
        playIntroSceneLine(scene, lineIndex)
    end
end)

addEvent("tagup:introSceneAudioFinished", true)
addEventHandler("tagup:introSceneAudioFinished", resourceRoot, function(sceneId, lineIndex, result, elapsed)
    local player, scene = client, mission.introScene
    lineIndex = tonumber(lineIndex)
    if source ~= resourceRoot or not scene or mission.stage ~= "intro" or scene.id ~= tonumber(sceneId) or
        scene.lineIndex ~= lineIndex or not scene.audioStarted or not isMissionPlayer(player) or scene.audioFinishedPlayers[player] then
        return
    end
    if result ~= "finished" then
        return failIntroScene(scene, ("La replique d'intro %d a echoue sur un client: %s"):format(
                                  lineIndex or -1, tostring(result)))
    end
    scene.audioFinishedPlayers[player] = true
    outputDebugString(("[tagging-up-turf] Intro world scene #%d line=%d natural finish on %s after %s ms"):format(
                          scene.id, lineIndex, getPlayerName(player), tostring(elapsed or "?")))
    if not allIntroScenePlayers(scene, "audioFinishedPlayers") then
        return
    end
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
        scene.audioGuardTimer = nil
    end
    if lineIndex == 1 then
        for _, member in ipairs(scene.players) do
            if isElement(member) then
                triggerClientEvent(member, "tagup:introSceneTrack", resourceRoot, scene.id)
            end
        end
    end
    if lineIndex < #TAGUP.introScene.audio then
        scene.nextLineTimer = rememberTimer(setTimer(function(expectedId, nextLine)
            local active = mission.introScene
            if active and active.id == expectedId then
                prepareIntroSceneLine(active, nextLine)
            end
        end, TAGUP.introScene.audioGap, 1, scene.id, lineIndex + 1))
    else
        scene.releaseTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.introScene
            if not active or active.id ~= expectedId then
                return
            end
            local sweet = mission.entities.sweet
            if not isElement(sweet) then
                return failIntroScene(active, "Sweet a disparu avant le placement final de la scene d'intro.")
            end
            local final = TAGUP.introScene.sweetFinal
            setElementPosition(sweet, final.x, final.y, final.z)
            setElementRotation(sweet, 0, 0, final.heading)
            active.finalWaitElapsed = true
            if active.entryAccepted then
                requestIntroSceneRelease(active)
            elseif not active.entryReported then
                active.entryReportGuardTimer = rememberTimer(setTimer(function(guardedId)
                    local guarded = mission.introScene
                    if guarded and guarded.id == guardedId and not guarded.entryReported then
                        failIntroScene(guarded, "Sweet n'a pas accepte sa task d'entree passager pendant SWE1_AE.")
                    end
                end, TAGUP.introScene.entryRequestTimeout, 1, active.id))
            end
        end, TAGUP.introScene.postAudioWait, 1, scene.id))
    end
end)

addEvent("tagup:introSceneEntryRequested", true)
addEventHandler("tagup:introSceneEntryRequested", resourceRoot, function(sceneId, sweet, vehicle, accepted)
    local scene = mission.introScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or scene.lineIndex ~= 5 or
        sweet ~= mission.entities.sweet or vehicle ~= mission.entities.vehicle or scene.entryReported then
        return
    end
    scene.entryReported = true
    scene.entryAccepted = accepted == true
    outputDebugString(("[tagging-up-turf] Intro world scene #%d Sweet passenger task accepted=%s"):format(
                          scene.id, tostring(scene.entryAccepted)))
    if not scene.entryAccepted then
        return failIntroScene(scene, "La task native d'entree passager de Sweet a ete refusee pendant SWE1_AE.")
    end
    mission.introEntryPending = true
    if isTimer(scene.entryReportGuardTimer) then
        killTimer(scene.entryReportGuardTimer)
        scene.entryReportGuardTimer = nil
    end
    if scene.finalWaitElapsed then
        requestIntroSceneRelease(scene)
    end
end)

addEvent("tagup:introSceneLeaseLost", true)
addEventHandler("tagup:introSceneLeaseLost", resourceRoot, function(sceneId)
    local scene = mission.introScene
    if source == resourceRoot and scene and scene.id == tonumber(sceneId) and isMissionPlayer(client) then
        failIntroScene(scene, "Un client a perdu la camera native pendant la scene d'intro.")
    end
end)

addEvent("tagup:introSceneReleased", true)
addEventHandler("tagup:introSceneReleased", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.introScene
    if source ~= resourceRoot or not scene or not scene.releasing or scene.id ~= tonumber(sceneId) or
        not isMissionPlayer(player) or scene.releasedPlayers[player] then
        return
    end
    if result ~= "released" then
        return failIntroScene(scene, "Un client n'a pas pu restaurer la camera d'intro.")
    end
    scene.releasedPlayers[player] = true
    if not allIntroScenePlayers(scene, "releasedPlayers") then
        return
    end

    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    local seated = isElement(sweet) and getPedOccupiedVehicle(sweet) == vehicle and getPedOccupiedVehicleSeat(sweet) == 1
    cancelIntroScene("completed", false)
    mission.introEntryPending = not seated
    for _, member in ipairs(mission.party) do
        if isElement(member) then
            setElementFrozen(member, false)
        end
    end
    setStage("enter_car", {message = seated and "Sweet est a bord. Montez dans la Greenwood." or
                                      "Montez dans la Greenwood pendant que Sweet prend sa place."})
    if not seated then
        mission.introEntryGuardTimer = rememberTimer(setTimer(function()
            if mission.running and mission.stage == "enter_car" and mission.introEntryPending then
                mission.introEntryPending = false
                failMission("Sweet n'a pas termine son entree passager dans le delai SCM.")
            end
        end, TAGUP.introScene.entryTimeout, 1))
    end
    outputDebugString(("[tagging-up-turf] Intro world scene #%d completed; Sweet passenger seated=%s"):format(
                          tonumber(sceneId), tostring(seated)))
end)

local function cancelFinalScene(reason, notifyClients)
    local scene = mission.finalScene
    if not scene then
        return
    end

    mission.finalScene = nil
    for _, timer in ipairs({scene.readyGuardTimer, scene.visualGuardTimer, scene.startTimer, scene.audioGuardTimer, scene.nextLineTimer,
                            scene.handshakeGuardTimer, scene.taskReportGuardTimer, scene.postAudioTimer, scene.releaseGuardTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if notifyClients ~= false then
        for _, player in ipairs(scene.players) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:finalSceneCancel", resourceRoot, scene.id, reason or "server_cancelled")
            end
        end
    end
    if isElement(mission.leader) then
        setPedAnimation(mission.leader, false)
    end
    if isElement(mission.entities.sweet) then
        setPedAnimation(mission.entities.sweet, false)
    end
    if isElement(mission.entities.vehicle) then
        setElementFrozen(mission.entities.vehicle, false)
    end
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            setElementFrozen(player, false)
        end
    end
end

local function allFinalScenePlayers(scene, field)
    for _, player in ipairs(scene.players) do
        if isElement(player) and not scene[field][player] then
            return false
        end
    end
    return true
end

local function failFinalScene(scene, reason)
    if mission.finalScene ~= scene then
        return
    end
    cancelFinalScene("failed", true)
    failMission(reason)
end

local prepareFinalSceneLine
local requestFinalSceneRelease

local function stageFinalSceneActors(scene, reason)
    if mission.finalScene ~= scene or not isElement(mission.leader) or not isElement(mission.entities.sweet) then
        return false
    end
    local profile = TAGUP.finalScene
    local leader, sweet = mission.leader, mission.entities.sweet
    local leaderX, leaderY, leaderZ = getElementPosition(leader)
    local sweetX, sweetY, sweetZ = getElementPosition(sweet)
    local distanceBefore = tagupDistance3D(leaderX, leaderY, leaderZ, sweetX, sweetY, sweetZ)
    local placementZOffset = profile.placementZOffset

    -- Both actors remain live synchronized peds during the dialogue. Reapply
    -- the SCM pair immediately before the handshake so collision or residual
    -- velocity cannot open a visible gap between the paired animations.
    setElementPosition(leader, profile.leader.x, profile.leader.y, profile.leader.z + placementZOffset)
    setElementRotation(leader, 0, 0, profile.leader.heading)
    setElementVelocity(leader, 0, 0, 0)
    setElementFrozen(leader, false)
    setElementPosition(sweet, profile.sweet.x, profile.sweet.y, profile.sweet.z + placementZOffset)
    setElementRotation(sweet, 0, 0, profile.sweet.heading)
    setElementVelocity(sweet, 0, 0, 0)
    setElementFrozen(sweet, false)

    local stagedLeaderX, stagedLeaderY, stagedLeaderZ = getElementPosition(leader)
    local stagedSweetX, stagedSweetY, stagedSweetZ = getElementPosition(sweet)
    local distanceAfter = tagupDistance3D(stagedLeaderX, stagedLeaderY, stagedLeaderZ, stagedSweetX, stagedSweetY, stagedSweetZ)
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d actor stage %s: distance %.3f -> %.3f m"):format(
                          scene.id, tostring(reason), distanceBefore, distanceAfter))
    return true
end

local function tryFinishFinalSceneTimeline(scene)
    if mission.finalScene ~= scene or scene.releasing or not scene.finalAudioFinished or not scene.walkAccepted or scene.postAudioTimer then
        return
    end
    scene.postAudioTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.finalScene
        if active and active.id == expectedId then
            requestFinalSceneRelease(active, false)
        end
    end, TAGUP.finalScene.postAudioWait, 1, scene.id))
end

local function playFinalSceneLine(scene, lineIndex)
    if mission.finalScene ~= scene or scene.lineIndex ~= lineIndex or scene.audioStarted or scene.releasing then
        return
    end
    local profile = TAGUP.finalScene
    scene.audioStarted = true
    scene.audioFinishedPlayers = {}
    scene.skippable = true
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
    end
    scene.audioGuardTimer = rememberTimer(setTimer(function(expectedId, expectedLine)
        local active = mission.finalScene
        if active and active.id == expectedId and active.lineIndex == expectedLine then
            failFinalScene(active, ("La replique finale %d a depasse son delai de garde."):format(expectedLine))
        end
    end, profile.audioFinishTimeout, 1, scene.id, lineIndex))

    if lineIndex == profile.leaderIdleChatLine and isElement(mission.leader) then
        if not setPedAnimation(mission.leader, "PED", "IDLE_CHAT", 6000, true, false, false, false, 250, false) then
            return failFinalScene(scene, "IDLE_CHAT a ete refusee pendant la scene finale.")
        end
    elseif lineIndex == profile.handshakeLine then
        local sweet = mission.entities.sweet
        if not stageFinalSceneActors(scene, "handshake") or not isElement(mission.leader) or not isElement(sweet) or
            not setPedAnimation(sweet, profile.handshake.block, profile.handshake.name, -1, false, false, false, false, 250, false) or
            not setPedAnimation(mission.leader, profile.handshake.block, profile.handshake.name, -1, false, false, false, false, 250, false) then
            return failFinalScene(scene, "La poignee de main GANGS a ete refusee pendant la scene finale.")
        end
        scene.handshakeGuardTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.finalScene
            if active and active.id == expectedId and not active.handshakeFinished then
                failFinalScene(active, "La poignee de main GANGS n'a pas termine dans le delai attendu.")
            end
        end, profile.handshake.guardTimeout, 1, scene.id))
        triggerClientEvent(mission.leader, "tagup:finalSceneObserveHandshake", resourceRoot, scene.id, sweet)
    end

    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:finalScenePlayAudio", resourceRoot, scene.id, lineIndex, player == mission.leader)
        end
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d playing %s"):format(scene.id, profile.audio[lineIndex].key))
end

prepareFinalSceneLine = function(scene, lineIndex)
    if mission.finalScene ~= scene or scene.releasing or not TAGUP.finalScene.audio[lineIndex] then
        return
    end
    scene.lineIndex = lineIndex
    scene.audioStarted = false
    scene.audioReadyPlayers = {}
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
    end
    scene.audioGuardTimer = rememberTimer(setTimer(function(expectedId, expectedLine)
        local active = mission.finalScene
        if active and active.id == expectedId and active.lineIndex == expectedLine then
            failFinalScene(active, ("Le chargement de la replique finale %d a depasse son delai de garde."):format(expectedLine))
        end
    end, TAGUP.finalScene.audioLoadTimeout, 1, scene.id, lineIndex))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:finalScenePrepareAudio", resourceRoot, scene.id, lineIndex)
        end
    end
end

requestFinalSceneRelease = function(scene, skipped)
    if mission.finalScene ~= scene or scene.releasing then
        return
    end
    scene.releasing = true
    scene.skipped = skipped == true
    scene.releasedPlayers = {}
    for _, timer in ipairs({scene.visualGuardTimer, scene.startTimer, scene.audioGuardTimer, scene.nextLineTimer, scene.handshakeGuardTimer,
                            scene.taskReportGuardTimer, scene.postAudioTimer}) do
        if isTimer(timer) then
            killTimer(timer)
        end
    end
    if isElement(mission.leader) then
        setPedAnimation(mission.leader, false)
    end
    if isElement(mission.entities.sweet) then
        setPedAnimation(mission.entities.sweet, false)
    end
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.finalScene
        if active and active.id == expectedId then
            failFinalScene(active, "La camera finale n'a pas ete restauree sur tous les clients.")
        end
    end, TAGUP.finalScene.releaseTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:finalSceneRelease", resourceRoot, scene.id, scene.skipped)
        end
    end
end

startFinalScene = function(extra)
    local leader, sweet, vehicle = mission.leader, mission.entities.sweet, mission.entities.vehicle
    if not isElement(leader) or not isElement(sweet) or not isElement(vehicle) then
        return failMission("Les acteurs de la scene finale ne sont plus disponibles.")
    end

    mission.finalSceneSerial = mission.finalSceneSerial + 1
    local scene = {
        id = mission.finalSceneSerial,
        players = {},
        readyPlayers = {},
        visualReadyPlayers = {},
        lineIndex = 1,
        traceExtra = extra,
    }
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            table.insert(scene.players, player)
        end
    end
    if #scene.players == 0 then
        return failMission("Aucun participant n'est disponible pour la scene finale.")
    end

    mission.finalScene = scene
    scene.readyGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.finalScene
        if active and active.id == expectedId then
            failFinalScene(active, "La preparation de la scene finale a depasse son delai de garde.")
        end
    end, TAGUP.finalScene.readyTimeout, 1, scene.id))
    setStage("final_scene", extra)
    for _, player in ipairs(scene.players) do
        triggerClientEvent(player, "tagup:finalScenePrepare", resourceRoot, scene.id, sweet, player == leader)
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d preparing fade, camera and SWE1_BN for %d participant(s)"):format(
                          scene.id, #scene.players))
end

addEvent("tagup:finalSceneReady", true)
addEventHandler("tagup:finalSceneReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.finalScene
    if source ~= resourceRoot or not scene or mission.stage ~= "final_scene" or scene.id ~= tonumber(sceneId) or
        not isMissionPlayer(player) or scene.readyPlayers[player] or scene.staged then
        return
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d ready player=%s result=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        return failFinalScene(scene, "La preparation de la scene finale a echoue sur un client: " .. tostring(result))
    end
    scene.readyPlayers[player] = true
    if not allFinalScenePlayers(scene, "readyPlayers") then
        return
    end

    if isTimer(scene.readyGuardTimer) then
        killTimer(scene.readyGuardTimer)
        scene.readyGuardTimer = nil
    end
    local profile, leader = TAGUP.finalScene, mission.leader
    removePedFromVehicle(leader)
    takeWeapon(leader, TAGUP.sprayWeapon)
    setPedWeaponSlot(leader, 0)
    setPedAnimation(leader, false)

    removePedFromVehicle(mission.entities.sweet)
    setElementSyncer(mission.entities.sweet, leader, true, true)
    takeWeapon(mission.entities.sweet, TAGUP.sprayWeapon)
    setPedWeaponSlot(mission.entities.sweet, 0)
    setPedAnimation(mission.entities.sweet, false)
    if not stageFinalSceneActors(scene, "initial") then
        return failFinalScene(scene, "Les acteurs de la scene finale n'ont pas pu etre places.")
    end
    setElementFrozen(mission.entities.vehicle, true)

    local extraIndex = 1
    for _, member in ipairs(scene.players) do
        if member ~= leader and isElement(member) then
            removePedFromVehicle(member)
            local position = profile.extraPlayers[extraIndex] or profile.extraPlayers[#profile.extraPlayers]
            extraIndex = extraIndex + 1
            setElementPosition(member, position.x, position.y, position.z)
            setElementRotation(member, 0, 0, position.heading)
            setElementFrozen(member, true)
            takeWeapon(member, TAGUP.sprayWeapon)
            setPedWeaponSlot(member, 0)
        end
    end

    scene.staged = true
    scene.visualGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.finalScene
        if active and active.id == expectedId and not active.started then
            failFinalScene(active, "Le modele habille de CJ n'a pas ete rendu avant la scene finale.")
        end
    end, profile.visualReadyTimeout, 1, scene.id))
    for _, member in ipairs(scene.players) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:finalSceneStart", resourceRoot, scene.id)
        end
    end
end)

addEvent("tagup:finalSceneVisualReady", true)
addEventHandler("tagup:finalSceneVisualReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.finalScene
    local profile = TAGUP.finalScene
    if source ~= resourceRoot or not scene or mission.stage ~= "final_scene" or scene.id ~= tonumber(sceneId) or not scene.staged or
        scene.started or not isMissionPlayer(player) or scene.visualReadyPlayers[player] then
        return
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d CJ visual ready player=%s result=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 300)))
    if result ~= "ready" then
        return failFinalScene(scene, "Le modele habille de CJ n'a pas pu etre rendu sur un client: " .. tostring(result))
    end
    scene.visualReadyPlayers[player] = true
    if not allFinalScenePlayers(scene, "visualReadyPlayers") then
        return
    end

    if isTimer(scene.visualGuardTimer) then
        killTimer(scene.visualGuardTimer)
        scene.visualGuardTimer = nil
    end
    scene.started = true
    scene.startedAt = getTickCount()
    scene.audioReadyPlayers = scene.readyPlayers
    for _, member in ipairs(scene.players) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:finalSceneReveal", resourceRoot, scene.id)
        end
    end
    scene.startTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.finalScene
        if active and active.id == expectedId then
            playFinalSceneLine(active, 1)
        end
    end, profile.audioStartDelay, 1, scene.id))
end)

addEvent("tagup:finalSceneAudioReady", true)
addEventHandler("tagup:finalSceneAudioReady", resourceRoot, function(sceneId, lineIndex, result, details)
    local player, scene = client, mission.finalScene
    lineIndex = tonumber(lineIndex)
    if source ~= resourceRoot or not scene or mission.stage ~= "final_scene" or scene.id ~= tonumber(sceneId) or
        scene.lineIndex ~= lineIndex or scene.audioStarted or not isMissionPlayer(player) or scene.audioReadyPlayers[player] then
        return
    end
    if result ~= "ready" then
        return failFinalScene(scene, ("Le chargement de la replique finale %d a echoue sur un client: %s"):format(
                                  lineIndex or -1, tostring(result)))
    end
    scene.audioReadyPlayers[player] = true
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d line=%d ready on %s (%s)"):format(
                          scene.id, lineIndex, getPlayerName(player), tostring(details or ""):sub(1, 180)))
    if allFinalScenePlayers(scene, "audioReadyPlayers") then
        playFinalSceneLine(scene, lineIndex)
    end
end)

addEvent("tagup:finalSceneAudioFinished", true)
addEventHandler("tagup:finalSceneAudioFinished", resourceRoot, function(sceneId, lineIndex, result, elapsed)
    local player, scene = client, mission.finalScene
    lineIndex = tonumber(lineIndex)
    if source ~= resourceRoot or not scene or mission.stage ~= "final_scene" or scene.id ~= tonumber(sceneId) or
        scene.lineIndex ~= lineIndex or not scene.audioStarted or not isMissionPlayer(player) or scene.audioFinishedPlayers[player] then
        return
    end
    if result ~= "finished" then
        return failFinalScene(scene, ("La replique finale %d a echoue sur un client: %s"):format(lineIndex or -1, tostring(result)))
    end
    scene.audioFinishedPlayers[player] = true
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d line=%d natural finish on %s after %s ms"):format(
                          scene.id, lineIndex, getPlayerName(player), tostring(elapsed or "?")))
    if not allFinalScenePlayers(scene, "audioFinishedPlayers") then
        return
    end
    if isTimer(scene.audioGuardTimer) then
        killTimer(scene.audioGuardTimer)
        scene.audioGuardTimer = nil
    end

    local profile = TAGUP.finalScene
    if lineIndex == profile.handshakeLine then
        scene.handshakeAudioFinished = true
        if scene.handshakeFinished then
            prepareFinalSceneLine(scene, profile.walkLine)
        end
    elseif lineIndex == profile.walkLine then
        scene.finalAudioFinished = true
        if not scene.walkReported then
            scene.taskReportGuardTimer = rememberTimer(setTimer(function(expectedId)
                local active = mission.finalScene
                if active and active.id == expectedId and not active.walkReported then
                    failFinalScene(active, "La task de depart a pied de Sweet n'a pas ete acceptee a temps.")
                end
            end, profile.taskReportTimeout, 1, scene.id))
        end
        tryFinishFinalSceneTimeline(scene)
    else
        prepareFinalSceneLine(scene, lineIndex + 1)
    end
end)

addEvent("tagup:finalSceneHandshakeResult", true)
addEventHandler("tagup:finalSceneHandshakeResult", resourceRoot, function(sceneId, sweet, result, details)
    local scene = mission.finalScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or
        sweet ~= mission.entities.sweet or scene.lineIndex ~= TAGUP.finalScene.handshakeLine or scene.handshakeFinished then
        return
    end
    if result ~= "finished" then
        return failFinalScene(scene, "La poignee de main GANGS a ete interrompue: " .. tostring(details or result))
    end
    scene.handshakeFinished = true
    if isTimer(scene.handshakeGuardTimer) then
        killTimer(scene.handshakeGuardTimer)
        scene.handshakeGuardTimer = nil
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d handshake finished naturally"):format(scene.id))
    if scene.handshakeAudioFinished then
        prepareFinalSceneLine(scene, TAGUP.finalScene.walkLine)
    end
end)

addEvent("tagup:finalSceneWalkResult", true)
addEventHandler("tagup:finalSceneWalkResult", resourceRoot, function(sceneId, sweet, accepted)
    local scene = mission.finalScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or
        sweet ~= mission.entities.sweet or scene.lineIndex ~= TAGUP.finalScene.walkLine or scene.walkReported then
        return
    end
    scene.walkReported = true
    scene.walkAccepted = accepted == true and getElementSyncer(sweet) == mission.leader
    if isTimer(scene.taskReportGuardTimer) then
        killTimer(scene.taskReportGuardTimer)
        scene.taskReportGuardTimer = nil
    end
    if not scene.walkAccepted then
        return failFinalScene(scene, "La task native de depart a pied de Sweet a ete refusee.")
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d Sweet walk task accepted"):format(scene.id))
    tryFinishFinalSceneTimeline(scene)
end)

addEvent("tagup:finalSceneSkipRequest", true)
addEventHandler("tagup:finalSceneSkipRequest", resourceRoot, function(sceneId)
    local scene = mission.finalScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.skippable or scene.releasing then
        return
    end
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d skip authorized by leader"):format(scene.id))
    requestFinalSceneRelease(scene, true)
end)

addEvent("tagup:finalSceneLeaseLost", true)
addEventHandler("tagup:finalSceneLeaseLost", resourceRoot, function(sceneId)
    local scene = mission.finalScene
    if source == resourceRoot and scene and scene.id == tonumber(sceneId) and isMissionPlayer(client) then
        failFinalScene(scene, "Un client a perdu la camera native pendant la scene finale.")
    end
end)

addEvent("tagup:finalSceneReleased", true)
addEventHandler("tagup:finalSceneReleased", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.finalScene
    if source ~= resourceRoot or not scene or not scene.releasing or scene.id ~= tonumber(sceneId) or
        not isMissionPlayer(player) or scene.releasedPlayers[player] then
        return
    end
    if result ~= "released" then
        return failFinalScene(scene, "Un client n'a pas pu restaurer la camera finale.")
    end
    scene.releasedPlayers[player] = true
    if not allFinalScenePlayers(scene, "releasedPlayers") then
        return
    end

    local skipped = scene.skipped
    outputDebugString(("[tagging-up-turf] Final Grove scene #%d %s after %d ms; camera/audio cleanup acknowledged"):format(
                          scene.id, skipped and "skipped" or "completed", getTickCount() - (scene.startedAt or getTickCount())))
    cancelFinalScene("completed", false)
    finishMission(true, skipped and {traceSkipped = true} or nil)
end)

createScmChar = function(model, x, y, scriptZ, heading)
    -- GTA's CREATE_CHAR adds 1.0 to the script Z before placing the ped.
    -- MTA's createPed consumes the element Z directly, so this conversion
    -- belongs to the SCM opcode adapter rather than to createPed itself.
    return createPed(model, x, y, scriptZ + 1.0, heading)
end

local function spawnBallas()
    if isElement(mission.entities.enemy1) and isElement(mission.entities.enemy2) then
        return true
    end
    if isElement(mission.entities.enemy1) or isElement(mission.entities.enemy2) then
        return failMission("La creation des deux Ballas est dans un etat partiel.")
    end
    local positions = {
        {2400.45, -1470.39, 22.97, 82.40, 102},
        {2396.48, -1469.90, 22.99, 262.64, 103},
    }
    local enemies = {}
    for index, data in ipairs(positions) do
        local ped = createScmChar(data[5], data[1], data[2], data[3], data[4])
        if ped then
            setElementDimension(ped, TAGUP.dimension)
            setElementData(ped, "tagup.enemy", true, true)
            setElementData(ped, TAGUP.missionActorData, true, true)
            if isElement(mission.leader) then
                setElementSyncer(ped, mission.leader, true, true)
            end
            mission.entities["enemy" .. index] = ped
            table.insert(enemies, ped)
        end
    end
    if #enemies ~= 2 then
        for _, ped in ipairs(enemies) do
            if isElement(ped) then
                destroyElement(ped)
            end
        end
        mission.entities.enemy1 = nil
        mission.entities.enemy2 = nil
        failMission("Les deux Ballas requis n'ont pas pu etre crees.")
        return false
    end
    broadcastState({enemies = enemies, message = "Deux Ballas vous ont reperes."})
    mission.ballasEncounterSerial = mission.ballasEncounterSerial + 1
    mission.ballasEncounter = {
        id = mission.ballasEncounterSerial,
        enemies = enemies,
        phase = "chat",
        createdAt = getTickCount(),
    }
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasEncounterPrepare", resourceRoot, mission.ballasEncounter.id, enemies)
        end
    end
    return true
end

local function activeTagIds()
    if mission.stage == "tags_idlewood" then
        return {1, 2}
    elseif mission.stage == "tags_ballas" then
        return {3, 4}
    elseif mission.stage == "rooftop" then
        return {5}
    end
    return {}
end

local function currentGroupComplete()
    for _, tagId in ipairs(activeTagIds()) do
        if not mission.completedTags[tagId] then
            return false
        end
    end
    return true
end

local function allPostRoofPlayers(scene, field)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not scene[field][player] then
            return false
        end
    end
    return true
end

local function completePostRoofScene(scene)
    if mission.postRoofScene ~= scene then
        return
    end
    local extra = scene.extra
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d complete; camera restored for %d participant(s)'):format(
                          scene.id, #mission.party))
    cancelPostRoofScene("completed", false)
    cancelVehiclePlayback("completed")
    setStage("return_after_roof", extra)
end

local function requestPostRoofRelease(scene)
    if mission.postRoofScene ~= scene or scene.releasing then
        return
    end
    scene.releasing = true
    scene.releasedPlayers = {}
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.postRoofScene
        if active and active.id == expectedId then
            cancelPostRoofScene("release_timeout")
            failMission("La camera post-toit n'a pas ete restauree sur tous les clients.")
        end
    end, TAGUP.postRoofScene.camera.releaseTimeout, 1, scene.id))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:postRoofSceneRelease", resourceRoot, scene.id)
        end
    end
end

local function tryFinalizePostRoofScene()
    local scene = mission.postRoofScene
    if not scene or scene.finalizing or not scene.playbackValidated or not allPostRoofPlayers(scene, "audioFinishedPlayers") then
        return
    end
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) then
        cancelPostRoofScene("actors_missing")
        return failMission("Sweet ou la Greenwood a disparu pendant la scene post-toit.")
    end

    scene.finalizing = true
    removePedFromVehicle(sweet)
    if not warpSweetIntoFirstFreeSeat() then
        cancelPostRoofScene("passenger_warp_failed")
        return failMission("Sweet n'a pas pu reprendre sa place passager apres SWE1_BH.")
    end

    local livingFlats = {}
    for _, key in ipairs({"enemy1", "enemy2"}) do
        local ped = mission.entities[key]
        if isElement(ped) and not isPedDead(ped) then
            setElementSyncer(ped, mission.leader, true, true)
            table.insert(livingFlats, ped)
        end
    end
    if #livingFlats == 0 then
        return requestPostRoofRelease(scene)
    end
    triggerClientEvent(mission.leader, "tagup:postRoofFlatsWander", resourceRoot, scene.id, livingFlats)
end

local function tryStartPostRoofAudio(scene)
    if mission.postRoofScene ~= scene or scene.audioStarted or not scene.hornLeadElapsed or
        not allPostRoofPlayers(scene, "audioReadyPlayers") then
        return
    end
    scene.audioStarted = true
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d SWE1_BH load barrier passed; reporting second horn and playing audio'):format(scene.id))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:postRoofAudioStart", resourceRoot, scene.id, scene.vehicle)
        end
    end
end

local function beginPostRoofScene(extra, playbackValidated)
    if mission.postRoofScene or mission.stage ~= "rooftop" then
        return
    end
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) then
        return failMission("La Greenwood a disparu avant la scene post-toit.")
    end

    mission.postRoofSceneSerial = mission.postRoofSceneSerial + 1
    local scene = {
        id = mission.postRoofSceneSerial,
        vehicle = vehicle,
        extra = extra,
        playbackValidated = playbackValidated == true,
        cameraReadyPlayers = {},
        audioReadyPlayers = {},
        audioFinishedPlayers = {},
        requestedAt = getTickCount(),
    }
    mission.postRoofScene = scene
    scene.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.postRoofScene
        if active and active.id == expectedId then
            cancelPostRoofScene("server_timeout")
            failMission("La scene post-toit a depasse son delai de garde.")
        end
    end, TAGUP.postRoofScene.guardTimeout, 1, scene.id))

    outputDebugString(('[tagging-up-turf] Starting post-roof scene #%d, directional preload heading=%.4f, playbackValidated=%s'):format(
                          scene.id, TAGUP.postRoofScene.preload.heading, tostring(scene.playbackValidated)))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:postRoofScenePrepare", resourceRoot, scene.id, vehicle, TAGUP.postRoofScene)
        end
    end
end

addEvent("tagup:postRoofSceneReady", true)
addEventHandler("tagup:postRoofSceneReady", resourceRoot, function(sceneId, vehicle, result, details)
    local player, scene = client, mission.postRoofScene
    if source ~= resourceRoot or not scene or mission.stage ~= "rooftop" or scene.id ~= tonumber(sceneId) or scene.vehicle ~= vehicle or
        not isMissionPlayer(player) or scene.cameraReadyPlayers[player] then
        return
    end
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d camera player=%s result=%s (%s)'):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelPostRoofScene("client_prepare_" .. tostring(result))
        return failMission("La preparation post-toit a echoue sur un client: " .. tostring(result))
    end
    scene.cameraReadyPlayers[player] = true
    if allPostRoofPlayers(scene, "cameraReadyPlayers") and not scene.hornTimer then
        scene.hornTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.postRoofScene
            if not active or active.id ~= expectedId then
                return
            end
            active.hornLeadElapsed = true
            for _, member in ipairs(mission.party) do
                if isElement(member) then
                    triggerClientEvent(member, "tagup:postRoofFirstHorn", resourceRoot, active.id, active.vehicle)
                end
            end
            tryStartPostRoofAudio(active)
        end, TAGUP.postRoofScene.hornLeadDelay, 1, scene.id))
    end
end)

addEvent("tagup:postRoofSceneFailure", true)
addEventHandler("tagup:postRoofSceneFailure", resourceRoot, function(sceneId, result, details)
    local scene = mission.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(client) then
        return
    end
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d failed on %s: %s (%s)'):format(
                          scene.id, getPlayerName(client), tostring(result), tostring(details or ""):sub(1, 180)), 2)
    cancelPostRoofScene("client_failure_" .. tostring(result))
    failMission("La scene post-toit a echoue sur un client: " .. tostring(result))
end)

addEvent("tagup:postRoofAudioReady", true)
addEventHandler("tagup:postRoofAudioReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.postRoofScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or scene.audioReadyPlayers[player] then
        return
    end
    if result ~= "ready" then
        cancelPostRoofScene("client_audio_ready_" .. tostring(result))
        return failMission("Le chargement de SWE1_BH a echoue sur un client: " .. tostring(result))
    end
    scene.audioReadyPlayers[player] = true
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d SWE1_BH ready on %s (%s)'):format(
                          scene.id, getPlayerName(player), tostring(details or ""):sub(1, 180)))
    tryStartPostRoofAudio(scene)
end)

addEvent("tagup:postRoofAudioResult", true)
addEventHandler("tagup:postRoofAudioResult", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.postRoofScene
    if source ~= resourceRoot or not scene or not scene.audioStarted or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or
        scene.audioFinishedPlayers[player] then
        return
    end
    if result ~= "finished" then
        cancelPostRoofScene("client_audio_" .. tostring(result))
        return failMission("La replique SWE1_BH a echoue sur un client: " .. tostring(result))
    end
    scene.audioFinishedPlayers[player] = true
    outputDebugString(('[tagging-up-turf] Post-roof scene #%d SWE1_BH finished on %s (%s)'):format(
                          scene.id, getPlayerName(player), tostring(details or ""):sub(1, 180)))
    tryFinalizePostRoofScene()
end)

addEvent("tagup:postRoofFlatsResult", true)
addEventHandler("tagup:postRoofFlatsResult", resourceRoot, function(sceneId, result, details)
    local scene = mission.postRoofScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.finalizing then
        return
    end
    if result ~= "ready" then
        cancelPostRoofScene("flat_wander_" .. tostring(result))
        return failMission("La remise en Wander des Ballas a echoue: " .. tostring(details or result))
    end
    requestPostRoofRelease(scene)
end)

addEvent("tagup:postRoofSceneReleased", true)
addEventHandler("tagup:postRoofSceneReleased", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.postRoofScene
    if source ~= resourceRoot or not scene or not scene.releasing or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or
        scene.releasedPlayers[player] then
        return
    end
    if result ~= "released" then
        cancelPostRoofScene("client_release_" .. tostring(result))
        return failMission("Un client n'a pas pu restaurer la camera post-toit.")
    end
    scene.releasedPlayers[player] = true
    if allPostRoofPlayers(scene, "releasedPlayers") then
        completePostRoofScene(scene)
    end
end)

local function startVehiclePlaybackReturn(extra)
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if mission.vehiclePlayback or not isElement(mission.leader) or not isElement(sweet) or not isElement(vehicle) then
        return failMission("Sweet ou la Greenwood est indisponible pour le recording 207.")
    end

    mission.vehiclePlaybackSerial = mission.vehiclePlaybackSerial + 1
    local playback = {
        id = mission.vehiclePlaybackSerial,
        ped = sweet,
        vehicle = vehicle,
        extra = extra,
        requestedAt = getTickCount(),
    }
    mission.vehiclePlayback = playback
    setElementFrozen(vehicle, false)
    setElementFrozen(sweet, false)
    setVehicleLocked(vehicle, false)
    mission.vehiclePlayerOnlyLocked = false
    mission.offscreenStored = false
    broadcastState()
    setElementSyncer(sweet, mission.leader, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)

    playback.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.vehiclePlayback
        if not mission.running or mission.stage ~= "rooftop" or not active or active.id ~= expectedId then
            return
        end
        cancelVehiclePlayback("server_timeout")
        failMission("Le recording 207 a depasse son delai de garde.")
    end, TAGUP.vehicleRecording207.guardTimeout, 1, playback.id))

    outputDebugString(("[tagging-up-turf] Requesting recorded-car return #%d (recording=%d) from %s"):format(
                          playback.id, TAGUP.vehicleRecording207.id, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:vehiclePlaybackPrepare", resourceRoot, playback.id, sweet, vehicle, TAGUP.vehicleRecording207)
end

local function advanceAfterTags(extra)
    if mission.stage == "tags_idlewood" then
        if isElement(mission.entities.vehicle) then
            setVehicleLocked(mission.entities.vehicle, false)
        end
        mission.vehiclePlayerOnlyLocked = false
        setStage("return_car", extra)
    elseif mission.stage == "tags_ballas" and mission.ballasGangSceneCompleted then
        local encounter = mission.ballasEncounter
        if encounter and not encounter.attackReady and not (extra and extra.traceSkipped) then
            if not encounter.rooftopPending then
                outputDebugString(('[tagging-up-turf] Ballas encounter #%d holding rooftop transition until native attack ACK'):format(
                                      encounter.id))
            end
            encounter.rooftopPending = true
            encounter.rooftopExtra = extra
            return
        end
        setStage("rooftop", extra)
    elseif mission.stage == "rooftop" then
        startVehiclePlaybackReturn(extra)
    end
end

failMission = function(reason, failureTextKey)
    if not mission.running or mission.finishing or mission.stage == "failed" then
        return
    end
    -- A failure must not leave a resource-owned control inhibitor alive during
    -- the failure banner. The scene cancel restores every local camera lease.
    cancelIntroScene("mission_failed")
    cancelDemoLeave("mission_failed")
    cancelDemoWalk("mission_failed")
    cancelDemoShoot("mission_failed")
    cancelDemoEnter("mission_failed")
    cancelDemoScene("mission_failed")
    cancelBallasGangScene("mission_failed")
    cancelBallasEncounter("mission_failed")
    cancelPostRoofScene("mission_failed")
    cancelVehiclePlayback("mission_failed")
    cancelFinalScene("mission_failed")
    cancelTransitionAudio("mission_failed")
    mission.stage = "failed"
    broadcastState({failureReason = reason or "La mission a echoue.", failureTextKey = failureTextKey})
    outputDebugString("[tagging-up-turf] Failed: " .. tostring(reason))
    rememberTimer(setTimer(function()
        finishMission(false)
    end, 3500, 1))
end

local sprayRequiredStages = {
    enter_car = true,
    drive_idlewood = true,
    demo = true,
    tags_idlewood = true,
    return_car = true,
    drive_ballas = true,
    ballas_departure = true,
    tags_ballas = true,
    rooftop = true,
}

local function hasWeapon(ped, weapon)
    for slot = 0, 12 do
        if getPedWeapon(ped, slot) == weapon then
            return true
        end
    end
    return false
end

local function isSpraycanRequired()
    return sprayRequiredStages[mission.stage] and not (mission.stage == "rooftop" and mission.completedTags[3])
end

local function startSpraycanMonitor()
    -- SWEET1 checks CJ's inventory in every active gameplay loop through the
    -- rooftop tag, then deliberately stops checking before recording 207.
    rememberTimer(setTimer(function()
        local leader = mission.leader
        if mission.running and not mission.finishing and isSpraycanRequired() and isElement(leader) and
            not hasWeapon(leader, TAGUP.sprayWeapon) then
            failMission("CJ n'a plus de bombe de peinture.", "SWE1SPF")
        end
    end, 100, 0))
end

local function grantWeaponAmmo(ped, weapon, ammo, setAsCurrent)
    local remaining = ammo
    local firstGrant = true
    while remaining > 0 do
        local granted = math.min(remaining, 9999)
        if not giveWeapon(ped, weapon, granted, setAsCurrent == true and firstGrant) then
            return false
        end
        firstGrant = false
        remaining = remaining - granted
    end
    return true
end

addEvent("tagup:vehiclePlaybackResult", true)
addEventHandler("tagup:vehiclePlaybackResult", resourceRoot, function(playbackId, ped, vehicle, result, details, elapsed)
    local player = client
    local playback = mission.vehiclePlayback
    if source ~= resourceRoot or not mission.running or mission.stage ~= "rooftop" or player ~= mission.leader or not playback or
        playback.id ~= tonumber(playbackId) or playback.ped ~= ped or playback.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized vehicle-playback result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Recording 207 #%d result=%s elapsed=%s (%s)"):format(
                          playback.id, tostring(result), tostring(elapsed or "-"), tostring(details or ""):sub(1, 180)))

    if result == "ready" then
        if playback.ready or getElementSyncer(ped) ~= player or getElementSyncer(vehicle) ~= player then
            cancelVehiclePlayback("invalid_ready")
            return failMission("Le recording 207 n'a pas ete prepare par le double syncer attendu.")
        end

        playback.ready = true
        -- SWEET1 clears Sweet's task and warps him into the driver seat before
        -- 05EB. The vehicle itself is not placed here: recording frame zero
        -- performs the native repositioning that replaces the old Lua teleport.
        removePedFromVehicle(ped)
        warpPedIntoVehicle(ped, vehicle, 0)
        setElementSyncer(ped, player, true, true)
        setElementSyncer(vehicle, player, true, true)
        if getVehicleOccupant(vehicle, 0) ~= ped then
            cancelVehiclePlayback("driver_warp_failed")
            return failMission("Sweet n'a pas pu prendre le volant avant le recording 207.")
        end

        playback.startTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.vehiclePlayback
            if not active or active.id ~= expectedId or not isElement(mission.leader) then
                return
            end
            triggerClientEvent(mission.leader, "tagup:vehiclePlaybackStart", resourceRoot, active.id, active.ped, active.vehicle,
                               TAGUP.vehicleRecording207)
        end, 200, 1, playback.id))
        return
    end

    if result == "started" then
        if not playback.ready or playback.started then
            cancelVehiclePlayback("invalid_start")
            return failMission("Le demarrage du recording 207 est incoherent.")
        end
        playback.started = true
        playback.startedAt = getTickCount()
        playback.sceneStartTimer = rememberTimer(setTimer(function(expectedId)
            local active = mission.vehiclePlayback
            if active and active.id == expectedId and mission.stage == "rooftop" then
                beginPostRoofScene(active.extra, false)
            end
        end, TAGUP.postRoofScene.startDelay, 1, playback.id))
        return
    end

    if result == "completed" then
        if not playback.started or playback.completing then
            cancelVehiclePlayback("invalid_completion")
            return failMission("La fin du recording 207 est incoherente.")
        end
        playback.completing = true
        local elapsedMs = tonumber(elapsed) or 0
        playback.completionTimer = rememberTimer(setTimer(function(expectedId, reportedElapsed)
            local active = mission.vehiclePlayback
            if not active or active.id ~= expectedId or not isElement(active.vehicle) or not isElement(active.ped) then
                return
            end
            local x, y, z = getElementPosition(active.vehicle)
            local endpoint = TAGUP.vehicleRecording207.endPosition
            local distance = getDistanceBetweenPoints3D(x, y, z, endpoint[1], endpoint[2], endpoint[3])
            local valid = reportedElapsed >= TAGUP.vehicleRecording207.minimumElapsed and reportedElapsed <= TAGUP.vehicleRecording207.maximumElapsed and
                              distance <= TAGUP.vehicleRecording207.serverEndRadius and getElementSyncer(active.vehicle) == mission.leader
            if not valid then
                cancelVehiclePlayback("invalid_completion")
                return failMission(("Le recording 207 a fini hors profil (%.2f m, %d ms)."):format(distance, reportedElapsed))
            end

            active.playbackValidated = true
            if isTimer(active.guardTimer) then
                killTimer(active.guardTimer)
                active.guardTimer = nil
            end
            if not mission.postRoofScene then
                beginPostRoofScene(active.extra, true)
            elseif mission.postRoofScene then
                mission.postRoofScene.playbackValidated = true
            end
            outputDebugString(("[tagging-up-turf] Recording 207 #%d completed at %.2f m after %d ms; waiting for SWE1_BH natural finish"):format(
                                  active.id, distance, reportedElapsed))
            tryFinalizePostRoofScene()
        end, 750, 1, playback.id, elapsedMs))
        return
    end

    cancelVehiclePlayback(tostring(result))
    failMission("Le recording 207 a echoue: " .. tostring(result) .. " (" .. tostring(details or "") .. ")")
end)

finishMission = function(passed, traceExtra)
    if not mission.running or mission.finishing then
        return
    end

    mission.finishing = true
    cancelFileCutscene("mission_finished")
    cancelIntroScene("mission_finished")
    cancelDemoLeave("mission_finished")
    cancelDemoWalk("mission_finished")
    cancelDemoShoot("mission_finished")
    cancelDemoEnter("mission_finished")
    cancelDemoScene("mission_finished")
    cancelBallasDeparture("mission_finished")
    cancelBallasGangScene("mission_finished")
    cancelBallasEncounter("mission_finished")
    cancelPostRoofScene("mission_finished")
    cancelVehiclePlayback("mission_finished")
    cancelFinalScene("mission_finished")
    cancelTransitionAudio("mission_finished")
    clearMissionTimers()
    if passed then
        mission.stage = "complete"
        -- Failure paths already log their terminal state; keep successful runs
        -- equally visible so a complete manual mission can be audited afterward.
        outputDebugString(("[tagging-up-turf] Mission passed: rewarding %d participant(s)."):format(#mission.party))
        broadcastState(traceExtra)
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                givePlayerMoney(player, 200)
            end
        end
    end

    local party = mission.party
    local snapshots = mission.snapshots
    destroyMissionEntities()
    -- failMission spends 3500 ms in the failed stage before entering this
    -- cleanup. Retain the party for another 1500 ms so GTA's native 5000 ms
    -- M_FAIL print is not cleared early by the client stop event.
    local delay = passed and 6000 or 1500
    setTimer(function()
        for _, player in ipairs(party) do
            if isElement(player) then
                restorePlayer(player, snapshots[player])
                triggerClientEvent(player, "tagup:stop", resourceRoot, passed)
            end
        end
        mission.running = false
        mission.finishing = false
        mission.stage = nil
        mission.leader = nil
        mission.party = {}
        mission.snapshots = {}
        mission.tagProgress = {}
        mission.completedTags = {}
        mission.demoLeave = nil
        mission.demoWalk = nil
        mission.demoShoot = nil
        mission.demoEnter = nil
        mission.demoScene = nil
        mission.ballasDeparture = nil
        mission.ballasGangScene = nil
        mission.ballasGangSceneCompleted = false
        mission.ballasEncounter = nil
        mission.ballasTimerResetAt = nil
        mission.checkpointGroundPending = {}
        mission.vehiclePlayback = nil
        mission.postRoofScene = nil
        mission.introScene = nil
        mission.introEntryPending = false
        mission.introEntryGuardTimer = nil
        mission.fileCutscene = nil
        mission.finalScene = nil
        mission.transitionAudio = nil
        mission.reminderIndex = 1
        mission.offscreenStored = false
        mission.vehiclePlayerOnlyLocked = false
    end, delay, 1)
end

local function setupMissionPlayers(useCutsceneCJ)
    local offsets = {
        {2514.0, -1666.6, 13.4, 90},
        {2514.0, -1668.0, 13.4, 90},
        {2514.0, -1669.4, 13.4, 90},
    }
    for index, player in ipairs(mission.party) do
        mission.snapshots[player] = snapshotPlayer(player)
        if player == mission.leader or useCutsceneCJ then
            -- Mark before mutation so even a partial clothing failure restores
            -- the original appearance through the ordinary failure cleanup.
            mission.snapshots[player].cjAppearanceApplied = true
            local applied, details = applyMissionCJ(player)
            if not applied then
                outputDebugString("[tagging-up-turf] CJ appearance setup failed: " .. tostring(details), 2)
                return false
            end
        end
        removePedFromVehicle(player)
        setElementInterior(player, 0)
        setElementDimension(player, TAGUP.dimension)
        setElementPosition(player, offsets[index][1], offsets[index][2], offsets[index][3])
        setElementRotation(player, 0, 0, offsets[index][4])
        -- SWEET1 preserves CJ's health, armour and existing inventory. MTA
        -- caps one giveWeapon call at 9999 rounds, so split GTA's additive
        -- 30000-round grant without replacing the rest of the loadout.
        if not grantWeaponAmmo(player, TAGUP.sprayWeapon, 30000, true) then
            return false
        end
        setElementFrozen(player, true)
    end
    return true
end

createMissionEntities = function(requester)
    local vehicle = createVehicle(TAGUP.vehicleModel, TAGUP.start[1], TAGUP.start[2], TAGUP.start[3], 0, 0, TAGUP.start[4])
    if not vehicle then
        return false
    end
    setElementDimension(vehicle, TAGUP.dimension)
    setVehicleColor(vehicle, unpack(TAGUP.vehicleColors))
    setVehiclePlateText(vehicle, TAGUP.vehiclePlate)
    setVehicleEngineState(vehicle, true)
    mission.entities.vehicle = vehicle

    local sweetStart = TAGUP.introScene.sweetStart
    local sweet = createScmChar(TAGUP.sweetModel, sweetStart.x, sweetStart.y, sweetStart.z, sweetStart.heading)
    if not sweet then
        return false
    end
    setElementDimension(sweet, TAGUP.dimension)
    setElementData(sweet, "tagup.sweet", true, true)
    -- GTA's CREATE_CHAR marks story actors as PED_MISSION. Replicate the
    -- policy so every client applies it before becoming Sweet's syncer.
    setElementData(sweet, TAGUP.missionActorData, true, true)
    -- SWEET1 assigns Sweet's GANG2 motion group for the lifetime of this ped.
    setPedWalkingStyle(sweet, TAGUP.introScene.sweetWalkingStyle)
    setElementHealth(sweet, 500)
    if not grantWeaponAmmo(sweet, TAGUP.sprayWeapon, 30000, true) then
        return false
    end
    mission.entities.sweet = sweet
    setElementSyncer(sweet, requester)

    for _, tag in ipairs(TAGUP.tags) do
        mission.tagProgress[tag.id] = 0
        if not createTagObject(tag) then
            return false
        end
    end

    local demo = TAGUP.demoTag
    local demoObject = createObject(TAGUP.tagModel, demo.x, demo.y, demo.z, 0, 0, demo.rotation)
    if not demoObject then
        return false
    end
    setElementDimension(demoObject, TAGUP.dimension)
    setElementCollisionsEnabled(demoObject, false)
    setElementData(demoObject, "tagup.paintAlpha", 0, true)
    mission.entities.demoTag = demoObject
    return true
end

local function startMission(requester, checkpoint)
    if mission.running then
        outputChatBox("Tagging Up Turf est deja en cours.", requester, 255, 190, 80)
        return
    end

    mission.running = true
    mission.ballasGangSceneCompleted = false
    mission.ballasEncounter = nil
    mission.ballasTimerResetAt = nil
    mission.transitionAudio = nil
    mission.reminderIndex = 1
    mission.offscreenStored = false
    mission.vehiclePlayerOnlyLocked = false
    mission.leader = requester
    mission.party = {requester}
    for _, player in ipairs(getElementsByType("player")) do
        if player ~= requester and #mission.party < TAGUP.maximumPlayers then
            table.insert(mission.party, player)
        end
    end

    if not setupMissionPlayers(not checkpoint) then
        return failMission("L'apparence vanilla de CJ n'a pas pu etre appliquee au leader.")
    end
    startSpraycanMonitor()

    if not checkpoint then
        startFileCutscene()
        return
    end

    if not createMissionEntities(requester) then
        return failMission("Les entites du checkpoint n'ont pas pu etre creees.")
    end
    local vehicle = mission.entities.vehicle
    local sweet = mission.entities.sweet
    local demoObject = mission.entities.demoTag

    if checkpoint == "pickup" then
        for _, tagId in ipairs({1, 2, 3, 4, 5}) do
            mission.tagProgress[tagId] = 255
            mission.completedTags[tagId] = true
            replaceTagObject(tagId)
        end
        setElementData(demoObject, "tagup.paintAlpha", 255, true)

        local endpoint = TAGUP.vehicleRecording207.endPosition
        setElementPosition(vehicle, endpoint[1], endpoint[2], endpoint[3] + 3.0)
        setElementRotation(vehicle, 0, 0, TAGUP.postRoofScene.preload.heading)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, true)
        setElementSyncer(vehicle, requester, true, true)
        setElementSyncer(sweet, requester, true, true)
        if not warpPedIntoVehicle(sweet, vehicle, 0) then
            return failMission("Sweet n'a pas pu prendre le volant au checkpoint SWE1_BH.")
        end

        local positions = {
            {2384.0, -1525.0, endpoint[3], 180},
            {2385.2, -1525.0, endpoint[3], 180},
            {2386.4, -1525.0, endpoint[3], 180},
        }
        mission.checkpointGroundSerial = mission.checkpointGroundSerial + 1
        local groundToken = mission.checkpointGroundSerial
        mission.checkpointGroundPending = {}
        for index, player in ipairs(mission.party) do
            local position = positions[index]
            removePedFromVehicle(player)
            -- The full mission streams this block while recording 207 drives
            -- through it. A direct checkpoint must prove the same collision is
            -- resident before starting 0A0B under the black fade.
            setElementPosition(player, position[1], position[2], position[3] + 3.0)
            setElementRotation(player, 0, 0, position[4])
            setElementVelocity(player, 0, 0, 0)
            setElementFrozen(player, true)
            mission.checkpointGroundPending[player] = {
                token = groundToken,
                kind = "pickup",
                x = position[1],
                y = position[2],
                expectedGroundZ = position[3],
                heading = position[4],
            }
            triggerClientEvent(player, "tagup:checkpointGroundProbe", resourceRoot, groundToken, position[1], position[2], position[3])
        end

        setStage("rooftop", {deferTraceStep = true, message = "Checkpoint SWE1_BH en preparation."})
        outputDebugString(('[tagging-up-turf] SWE1_BH pickup checkpoint started by %s; waiting for destination collision'):format(
                              getPlayerName(requester)))
        outputChatBox("Checkpoint SWE1_BH charge. Attends la stabilisation du decor avant la scene.", requester, 120, 220, 120)
        return
    end

    if checkpoint == "idlewood" then
        -- Start inside the original arrival box with a small roll so the
        -- client can prove that 09D0 stays false after the first wheel touches
        -- and becomes true only once all four native contacts are active.
        setElementPosition(vehicle, TAGUP.idlewoodDestination[1], TAGUP.idlewoodDestination[2], TAGUP.idlewoodDestination[3] + 2.0)
        setElementRotation(vehicle, 8, 0, 270)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, false)

        if not warpPedIntoVehicle(requester, vehicle, 0) or not warpPedIntoVehicle(sweet, vehicle, 1) then
            return failMission("Le checkpoint 09D0 Idlewood n'a pas pu installer le conducteur ou Sweet.")
        end
        for index = 2, #mission.party do
            local player = mission.party[index]
            if not warpPedIntoVehicle(player, vehicle, index) then
                return failMission("Le checkpoint 09D0 Idlewood n'a pas pu installer toute l'equipe.")
            end
        end
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                setElementFrozen(player, false)
            end
        end

        setElementSyncer(sweet, requester, true, true)
        setStage("drive_idlewood", {message = "Checkpoint 09D0 Idlewood pret dans la zone d'arrivee."})
        outputDebugString(("[tagging-up-turf] Idlewood all-wheels checkpoint started by %s; exact LOCATE_CAR_3D + 09D0 gate remains armed"):format(
                              getPlayerName(requester)))
        outputChatBox("Checkpoint Idlewood charge. La scene doit attendre que les quatre roues touchent la route.", requester, 120, 220, 120)
        return
    end

    if checkpoint == "departure" then
        -- This checkpoint owns only mission setup. It leaves the real
        -- LOCATE_CAR_3D, camera, leave-car, audio, and DriveWander gates intact.
        for _, tagId in ipairs({1, 2}) do
            mission.tagProgress[tagId] = 255
            mission.completedTags[tagId] = true
            replaceTagObject(tagId)
        end
        setElementData(demoObject, "tagup.paintAlpha", 255, true)

        -- The checkpoint bypasses the drive that normally streams this road.
        -- Start above the SCM target so collision can settle the Greenwood onto
        -- the surface without losing the real 4 m arrival gate.
        setElementPosition(vehicle, TAGUP.ballasDestination[1], TAGUP.ballasDestination[2], TAGUP.ballasDestination[3] + 2.0)
        setElementRotation(vehicle, 8, 0, 270)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, false)

        if not warpPedIntoVehicle(requester, vehicle, 0) or not warpPedIntoVehicle(sweet, vehicle, 1) then
            return failMission("Le checkpoint SWE1_AV n'a pas pu installer le conducteur ou Sweet.")
        end
        for index = 2, #mission.party do
            local player = mission.party[index]
            if not warpPedIntoVehicle(player, vehicle, index) then
                return failMission("Le checkpoint SWE1_AV n'a pas pu installer toute l'equipe.")
            end
        end
        for _, player in ipairs(mission.party) do
            if isElement(player) then
                setElementFrozen(player, false)
            end
        end

        setElementSyncer(sweet, requester, true, true)
        mission.ballasTimerResetAt = getTickCount()
        setStage("drive_ballas", {message = "Checkpoint SWE1_AV pret dans la zone d'arrivee Ballas."})
        outputDebugString(("[tagging-up-turf] SWE1_AV departure checkpoint started by %s; real LOCATE_CAR_3D gate remains armed"):format(
                              getPlayerName(requester)))
        outputChatBox("Checkpoint SWE1_AV charge. Attends la stabilisation de la Greenwood pour declencher la scene.", requester, 120, 220, 120)
        return
    end

    if checkpoint == "ballas" then
        -- This checkpoint preserves the state produced by the completed
        -- Idlewood and first Ballas tag flow while leaving the encounter's
        -- spawn, chat, camera, approach, follow, and attack gates untouched.
        for _, tagId in ipairs({1, 2, 4}) do
            mission.tagProgress[tagId] = 255
            mission.completedTags[tagId] = true
            replaceTagObject(tagId)
        end
        setElementData(demoObject, "tagup.paintAlpha", 255, true)

        setElementPosition(vehicle, TAGUP.ballasDestination[1], TAGUP.ballasDestination[2], TAGUP.ballasDestination[3])
        setElementRotation(vehicle, 0, 0, 270)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementSyncer(vehicle, requester, true, true)
        setElementSyncer(sweet, requester, true, true)
        if not warpSweetIntoFirstFreeSeat() then
            return failMission("Sweet n'a pas pu etre initialise au checkpoint Ballas.")
        end

        local checkpointPositions = {
            {2373.0, -1470.52, 22.97, 90},
            {2371.8, -1471.72, 22.97, 90},
            {2371.8, -1469.32, 22.97, 90},
        }
        mission.checkpointGroundSerial = mission.checkpointGroundSerial + 1
        local groundToken = mission.checkpointGroundSerial
        mission.checkpointGroundPending = {}
        for index, player in ipairs(mission.party) do
            if isElement(player) then
                local position = checkpointPositions[index]
                removePedFromVehicle(player)
                -- Keep the player above the expected surface while the remote
                -- GTA sector streams. setupMissionPlayers already froze the
                -- actor, which intentionally bypasses MTA's long-teleport
                -- waiting helper, so the owning client must prove collision is
                -- available before the server releases the player.
                setElementPosition(player, position[1], position[2], position[3] + 2.0)
                setElementRotation(player, 0, 0, position[4])
                setElementVelocity(player, 0, 0, 0)
                setElementFrozen(player, true)
                mission.checkpointGroundPending[player] = {
                    token = groundToken,
                    x = position[1],
                    y = position[2],
                    expectedGroundZ = position[3],
                    heading = position[4],
                }
                triggerClientEvent(player, "tagup:checkpointGroundProbe", resourceRoot, groundToken, position[1], position[2], position[3])
            end
        end

        mission.ballasTimerResetAt = getTickCount()
        mission.vehiclePlayerOnlyLocked = true
        setStage("tags_ballas", {message = "Checkpoint Ballas pret. Avancez vers le tag de l'allee."})
        outputDebugString(('[tagging-up-turf] Ballas checkpoint started by %s outside the SCM 20x17 camera gate'):format(
                              getPlayerName(requester)))
        outputChatBox("Checkpoint Ballas charge. Avance vers le tag vert pour declencher la scene.", requester, 120, 220, 120)
        return
    end

    if checkpoint == "final" then
        for _, tagId in ipairs({1, 2, 3, 4, 5}) do
            mission.tagProgress[tagId] = 255
            mission.completedTags[tagId] = true
            replaceTagObject(tagId)
        end
        setElementData(demoObject, "tagup.paintAlpha", 255, true)

        setElementPosition(vehicle, TAGUP.homeDestination[1], TAGUP.homeDestination[2], TAGUP.homeDestination[3])
        setElementRotation(vehicle, 0, 0, 180)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, false)
        setElementSyncer(vehicle, requester, true, true)
        setElementSyncer(sweet, requester, true, true)
        if not warpPedIntoVehicle(requester, vehicle, 0) or not warpPedIntoVehicle(sweet, vehicle, 1) then
            return failMission("Le checkpoint final n'a pas pu installer CJ ou Sweet dans la Greenwood.")
        end
        for index = 2, #mission.party do
            local member = mission.party[index]
            if not warpPedIntoVehicle(member, vehicle, index) then
                return failMission("Le checkpoint final n'a pas pu installer toute l'equipe.")
            end
        end
        for _, member in ipairs(mission.party) do
            if isElement(member) then
                setElementFrozen(member, false)
            end
        end

        rememberTimer(setTimer(function()
            if mission.running and not mission.finalScene then
                startFinalScene({traceSkipped = true})
            end
        end, 1000, 1))
        outputDebugString(("[tagging-up-turf] Final Grove checkpoint started by %s; scene begins after streaming warmup"):format(
                              getPlayerName(requester)))
        outputChatBox("Checkpoint final charge. La scene Grove Street va commencer.", requester, 120, 220, 120)
        return
    end

end

addEvent("tagup:checkpointGroundReady", true)
addEventHandler("tagup:checkpointGroundReady", resourceRoot, function(token, reportedGroundZ)
    local player = client
    local pending = mission.checkpointGroundPending[player]
    local stageMatches = pending and ((pending.kind == "pickup" and mission.stage == "rooftop") or
                             (pending.kind ~= "pickup" and mission.stage == "tags_ballas"))
    if source ~= resourceRoot or not mission.running or not stageMatches or not isMissionPlayer(player) or pending.token ~= tonumber(token) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized checkpoint ground result", 2)
        return
    end

    local groundZ = tonumber(reportedGroundZ)
    if not groundZ or math.abs(groundZ - pending.expectedGroundZ) > 2.0 then
        mission.checkpointGroundPending[player] = nil
        return failMission(pending.kind == "pickup" and "La collision du checkpoint SWE1_BH n'a pas pu etre chargee." or
                               "La collision du checkpoint Ballas n'a pas pu etre chargee.")
    end

    setElementPosition(player, pending.x, pending.y, groundZ + 1.0)
    setElementRotation(player, 0, 0, pending.heading)
    setElementVelocity(player, 0, 0, 0)
    mission.checkpointGroundPending[player] = nil
    if pending.kind ~= "pickup" then
        setElementFrozen(player, false)
        return outputDebugString(("[tagging-up-turf] Ballas checkpoint ground ready for %s at Z=%.3f"):format(getPlayerName(player), groundZ))
    end

    outputDebugString(("[tagging-up-turf] SWE1_BH checkpoint collision ready for %s at Z=%.3f"):format(getPlayerName(player), groundZ))
    if next(mission.checkpointGroundPending) then
        return
    end

    -- Collision readiness proves the cold teleport has reached the same world
    -- block as the real recording. Keep one short streaming window before the
    -- black fade so the checkpoint does not manufacture a worst-case 0A0B.
    rememberTimer(setTimer(function(expectedToken)
        if not mission.running or mission.stage ~= "rooftop" or mission.checkpointGroundSerial ~= expectedToken or
            next(mission.checkpointGroundPending) then
            return
        end
        for _, member in ipairs(mission.party) do
            if isElement(member) then
                setElementFrozen(member, false)
            end
        end
        local endpoint = TAGUP.vehicleRecording207.endPosition
        local vehicle = mission.entities.vehicle
        if not isElement(vehicle) then
            return failMission("La Greenwood a disparu pendant le warmup du checkpoint SWE1_BH.")
        end
        setElementPosition(vehicle, endpoint[1], endpoint[2], endpoint[3])
        setElementRotation(vehicle, 0, 0, TAGUP.postRoofScene.preload.heading)
        setElementVelocity(vehicle, 0, 0, 0)
        setElementFrozen(vehicle, false)
        outputDebugString("[tagging-up-turf] SWE1_BH checkpoint streaming warmup complete; starting post-roof scene")
        beginPostRoofScene({traceSkipped = true}, true)
    end, 1500, 1, pending.token))
end)

local function allBallasGangScenePlayersReady(scene, field)
    for _, player in ipairs(scene.players) do
        if not isElement(player) or not scene[field][player] then
            return false
        end
    end
    return true
end

local function enableBallasApproach(reason)
    local encounter = mission.ballasEncounter
    if not encounter or (encounter.phase ~= "chat" and encounter.phase ~= "camera") then
        return false
    end

    encounter.phase = "awaiting_approach"
    encounter.approachEnabledAt = getTickCount()
    outputDebugString(('[tagging-up-turf] Ballas encounter #%d awaiting SCM 5x5 approach (%s)'):format(encounter.id,
                                                                                                      tostring(reason)))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasEncounterApproachEnabled", resourceRoot, encounter.id)
        end
    end
    return true
end

local function startBallasAttack(reason)
    local encounter = mission.ballasEncounter
    if not encounter or encounter.phase ~= "following" then
        return false
    end

    encounter.phase = "attacking"
    encounter.attackStartedAt = getTickCount()
    if isTimer(encounter.attackTimer) then
        killTimer(encounter.attackTimer)
        encounter.attackTimer = nil
    end
    outputDebugString(('[tagging-up-turf] Ballas encounter #%d starting native KillPedOnFoot after %d ms (%s)'):format(
                          encounter.id, getTickCount() - encounter.followStartedAt, tostring(reason)))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasEncounterAttack", resourceRoot, encounter.id, reason)
        end
    end
    return true
end

local function armBallasAttackCondition(encounter)
    if not encounter or encounter ~= mission.ballasEncounter or encounter.phase ~= "following" or not encounter.followReady then
        return
    end

    if isTimer(encounter.attackTimer) then
        killTimer(encounter.attackTimer)
        encounter.attackTimer = nil
    end
    if mission.completedTags[3] then
        return startBallasAttack("alley_tag_complete")
    end

    local secondFlatAlive = isElement(encounter.enemies[2]) and not isPedDead(encounter.enemies[2])
    if secondFlatAlive then
        mission.ballasTimerResetAt = getTickCount()
    end
    local elapsed = math.max(0, getTickCount() - (mission.ballasTimerResetAt or getTickCount()))
    local remaining = math.max(50, TAGUP.ballasGangScene.follow.attackDelay - elapsed)
    encounter.attackTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasEncounter
        if active and active.id == expectedId and active.phase == "following" and active.followReady then
            startBallasAttack("timer_5000")
        end
    end, remaining, 1, encounter.id))
end

local function completeBallasGangScene(scene)
    if mission.ballasGangScene ~= scene then
        return
    end

    local reason = scene.skipped and "skipped" or "completed"
    mission.ballasGangSceneCompleted = true
    outputDebugString(("[tagging-up-turf] Ballas gang scene #%d %s after %d ms; all camera leases acknowledged"):format(
                          scene.id, reason, getTickCount() - scene.startedAt))
    cancelBallasGangScene(reason)
    enableBallasApproach(reason)
    broadcastState({ballasGangSceneCompleted = true})
    if currentGroupComplete() then
        rememberTimer(setTimer(advanceAfterTags, 900, 1))
    end
end

local function releaseBallasGangSceneLeases(scene)
    if mission.ballasGangScene ~= scene or scene.releasing then
        return
    end

    scene.releasing = true
    scene.releasedPlayers = {}
    if isTimer(scene.finalCheckGuardTimer) then
        killTimer(scene.finalCheckGuardTimer)
    end
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasGangScene
        if not mission.running or mission.stage ~= "tags_ballas" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasGangScene("camera_release_timeout")
        failMission("La restauration de la camera Ballas a depasse le delai de garde.")
    end, TAGUP.ballasGangScene.finalCheckTimeout, 1, scene.id))

    local reason = scene.skipped and "skipped" or "completed"
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasGangSceneRelease", resourceRoot, scene.id, reason)
        end
    end
end

local function requestBallasGangSceneFinalCheck(scene, skipped)
    if mission.ballasGangScene ~= scene or scene.finalCheckRequested then
        return
    end

    scene.finalCheckRequested = true
    scene.skipped = skipped == true
    scene.finalReadyPlayers = {}
    if isTimer(scene.preSkipTimer) then
        killTimer(scene.preSkipTimer)
    end
    if isTimer(scene.completionTimer) then
        killTimer(scene.completionTimer)
    end
    scene.finalCheckGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasGangScene
        if not mission.running or mission.stage ~= "tags_ballas" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasGangScene("camera_final_check_timeout")
        failMission("La verification finale de la mini-scene Ballas a depasse le delai de garde.")
    end, TAGUP.ballasGangScene.finalCheckTimeout, 1, scene.id))

    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasGangSceneFinalCheck", resourceRoot, scene.id)
        end
    end
end

local function startBallasGangSceneTimeline(scene)
    if mission.ballasGangScene ~= scene or scene.started or not allBallasGangScenePlayersReady(scene, "readyPlayers") then
        return
    end

    scene.started = true
    scene.startedAt = getTickCount()
    if isTimer(scene.readyGuardTimer) then
        killTimer(scene.readyGuardTimer)
    end
    outputDebugString(("[tagging-up-turf] Ballas gang camera barrier #%d passed for %d participant(s)"):format(scene.id,
                                                                                                               #scene.players))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasGangSceneStart", resourceRoot, scene.id)
        end
    end

    -- SKIP_CUTSCENE_START follows the SCM's initial WAIT 500. The server owns
    -- the window so one co-op client cannot resume while the others stay frozen.
    scene.preSkipTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasGangScene
        if not active or active.id ~= expectedId or active.finalCheckRequested then
            return
        end
        active.skippable = true
        for _, player in ipairs(active.players) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:ballasGangSceneSkippable", resourceRoot, active.id,
                                   player == mission.leader)
            end
        end
        active.completionTimer = rememberTimer(setTimer(function(completionId)
            local completing = mission.ballasGangScene
            if completing and completing.id == completionId then
                requestBallasGangSceneFinalCheck(completing, false)
            end
        end, TAGUP.ballasGangScene.skippableDuration, 1, active.id))
    end, TAGUP.ballasGangScene.preSkipWait, 1, scene.id))
end

local function beginBallasGangScene()
    if not mission.running or mission.stage ~= "tags_ballas" or mission.ballasGangScene or mission.ballasGangSceneCompleted then
        return
    end

    local encounter = mission.ballasEncounter
    if not encounter or not encounter.chatReady then
        return
    end

    local enemies = {mission.entities.enemy1, mission.entities.enemy2}
    for _, ped in ipairs(enemies) do
        if not isElement(ped) then
            return failMission("Les deux Ballas requis pour la mini-scene n'ont pas pu etre crees.")
        end
        if isPedDead(ped) then
            -- The SCM consumes gang_hassle even when either alive check fails:
            -- the shot is skipped once and the tag loop continues.
            mission.ballasGangSceneCompleted = true
            outputDebugString("[tagging-up-turf] Ballas gang scene skipped because one Flat is dead")
            enableBallasApproach("one_flat_dead")
            broadcastState({ballasGangSceneCompleted = true})
            if currentGroupComplete() then
                rememberTimer(setTimer(advanceAfterTags, 900, 1))
            end
            return
        end
    end

    mission.ballasGangSceneSerial = mission.ballasGangSceneSerial + 1
    local scene = {
        id = mission.ballasGangSceneSerial,
        enemies = enemies,
        players = {unpack(mission.party)},
        readyPlayers = {},
        requestedAt = getTickCount(),
    }
    mission.ballasGangScene = scene
    if mission.ballasEncounter then
        mission.ballasEncounter.phase = "camera"
    end
    scene.readyGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasGangScene
        if not mission.running or mission.stage ~= "tags_ballas" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasGangScene("camera_ready_timeout")
        failMission("La mini-scene Ballas n'est pas prete sur tous les clients.")
    end, TAGUP.ballasGangScene.readyTimeout, 1, scene.id))

    outputDebugString(("[tagging-up-turf] Preparing Ballas gang scene #%d at SCM trigger %.2f, %.2f"):format(
                          scene.id, TAGUP.ballasGangScene.trigger.x, TAGUP.ballasGangScene.trigger.y))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasGangScenePrepare", resourceRoot, scene.id, enemies)
        end
    end
end

addEvent("tagup:ballasGangTrigger", true)
addEventHandler("tagup:ballasGangTrigger", resourceRoot, function()
    local player = client
    if source ~= resourceRoot or player ~= mission.leader or not isMissionPlayer(player) or not mission.running or
        mission.stage ~= "tags_ballas" or mission.ballasGangScene or mission.ballasGangSceneCompleted then
        return
    end

    local x, y = getElementPosition(player)
    local trigger = TAGUP.ballasGangScene.trigger
    if not isElement(mission.entities.enemy1) and math.abs(x - trigger.x) <= trigger.spawnRadiusX and
        math.abs(y - trigger.y) <= trigger.spawnRadiusY then
        spawnBallas()
    end
    if math.abs(x - trigger.x) <= trigger.radiusX and math.abs(y - trigger.y) <= trigger.radiusY then
        beginBallasGangScene()
    end
end)

addEvent("tagup:ballasEncounterTaskResult", true)
addEventHandler("tagup:ballasEncounterTaskResult", resourceRoot, function(encounterId, phase, result, details)
    local player = client
    local encounter = mission.ballasEncounter
    if source ~= resourceRoot or player ~= mission.leader or not isMissionPlayer(player) or not encounter or encounter.id ~= tonumber(encounterId) or
        type(phase) ~= "string" then
        return
    end

    outputDebugString(('[tagging-up-turf] Ballas encounter #%d phase=%s result=%s (%s)'):format(
                          encounter.id, phase, tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelBallasEncounter("native_" .. phase .. "_" .. tostring(result))
        return failMission("La task native Ballas a echoue pendant " .. phase .. ": " .. tostring(result))
    end
    encounter[phase .. "Ready"] = true
    if phase == "follow" and encounter.phase == "following" and isElement(encounter.enemies[2]) and not isPedDead(encounter.enemies[2]) then
        for _, member in ipairs(mission.party) do
            if isElement(member) then
                triggerClientEvent(member, "tagup:ballasEncounterAudioCue", resourceRoot, encounter.id, "whatTheFuck")
            end
        end
        armBallasAttackCondition(encounter)
    elseif phase == "follow" and encounter.phase == "following" then
        armBallasAttackCondition(encounter)
    elseif phase == "attack" and encounter.phase == "attacking" then
        local secondFlatAlive = isElement(encounter.enemies[2]) and not isPedDead(encounter.enemies[2])
        for _, member in ipairs(mission.party) do
            if isElement(member) then
                triggerClientEvent(member, secondFlatAlive and "tagup:ballasEncounterAudioCue" or "tagup:ballasEncounterSpeechRestore",
                                   resourceRoot, encounter.id, secondFlatAlive and "getThatFool" or nil)
            end
        end
        if encounter.rooftopPending and mission.ballasGangSceneCompleted and currentGroupComplete() then
            local rooftopExtra = encounter.rooftopExtra
            encounter.rooftopPending = false
            encounter.rooftopExtra = nil
            advanceAfterTags(rooftopExtra)
        end
    end
end)

addEvent("tagup:ballasEncounterApproach", true)
addEventHandler("tagup:ballasEncounterApproach", resourceRoot, function(encounterId)
    local player = client
    local encounter = mission.ballasEncounter
    if source ~= resourceRoot or player ~= mission.leader or not isMissionPlayer(player) or not mission.running or
        mission.stage ~= "tags_ballas" or not encounter or encounter.id ~= tonumber(encounterId) or encounter.phase ~= "awaiting_approach" then
        return
    end

    local x, y = getElementPosition(player)
    local approach = TAGUP.ballasGangScene.approach
    if math.abs(x - approach.x) > approach.radiusX or math.abs(y - approach.y) > approach.radiusY then
        local now = getTickCount()
        if not encounter.lastApproachRejectLogAt or now - encounter.lastApproachRejectLogAt >= 1000 then
            encounter.lastApproachRejectLogAt = now
            outputDebugString(('[tagging-up-turf] Ballas encounter #%d rejected SCM 5x5 approach: server=(%.2f, %.2f), delta=(%.2f, %.2f)'):format(
                                  encounter.id, x, y, x - approach.x, y - approach.y), 2)
        end
        return
    end

    encounter.phase = "following"
    encounter.followStartedAt = getTickCount()
    outputDebugString(('[tagging-up-turf] Ballas encounter #%d accepted SCM 5x5 approach from %s'):format(encounter.id,
                                                                                                        getPlayerName(player)))
    for _, member in ipairs(mission.party) do
        if isElement(member) then
            triggerClientEvent(member, "tagup:ballasEncounterFollow", resourceRoot, encounter.id)
        end
    end
end)

addEvent("tagup:ballasRespectHelpShown", true)
addEventHandler("tagup:ballasRespectHelpShown", resourceRoot, function()
    local player = client
    if source ~= resourceRoot or player ~= mission.leader or not mission.running or mission.stage ~= "tags_ballas" then
        return
    end
    local x, y = getElementPosition(player)
    if math.abs(x - 2353.30) <= 3 and math.abs(y + 1508.18) <= 3 then
        mission.ballasTimerResetAt = getTickCount()
        outputDebugString("[tagging-up-turf] Ballas TIMERA reset by SWE1_G gate")
    end
end)

addEvent("tagup:ballasGangSceneReady", true)
addEventHandler("tagup:ballasGangSceneReady", resourceRoot, function(sceneId, result, details)
    local player = client
    local scene = mission.ballasGangScene
    if source ~= resourceRoot or not mission.running or mission.stage ~= "tags_ballas" or not isMissionPlayer(player) or not scene or
        scene.id ~= tonumber(sceneId) or scene.started or scene.readyPlayers[player] then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas gang camera result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas gang camera #%d player=%s result=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelBallasGangScene("client_camera_" .. tostring(result))
        return failMission("La camera native de la mini-scene Ballas a echoue: " .. tostring(result))
    end
    scene.readyPlayers[player] = true
    startBallasGangSceneTimeline(scene)
end)

addEvent("tagup:ballasGangSceneLeaseLost", true)
addEventHandler("tagup:ballasGangSceneLeaseLost", resourceRoot, function(sceneId)
    local player = client
    local scene = mission.ballasGangScene
    if source ~= resourceRoot or not mission.running or mission.stage ~= "tags_ballas" or not isMissionPlayer(player) or not scene or
        scene.id ~= tonumber(sceneId) then
        return
    end
    outputDebugString(("[tagging-up-turf] Ballas gang camera #%d lease lost on %s"):format(scene.id, getPlayerName(player)), 2)
    cancelBallasGangScene("client_camera_lease_lost")
    failMission("Un client a perdu la camera native pendant la mini-scene Ballas.")
end)

addEvent("tagup:ballasGangSceneSkipRequest", true)
addEventHandler("tagup:ballasGangSceneSkipRequest", resourceRoot, function(sceneId)
    local player = client
    local scene = mission.ballasGangScene
    if source ~= resourceRoot or player ~= mission.leader or not isMissionPlayer(player) or not scene or scene.id ~= tonumber(sceneId) or
        not scene.skippable or scene.finalCheckRequested then
        return
    end
    outputDebugString(("[tagging-up-turf] Ballas gang scene #%d global skip accepted from leader %s"):format(scene.id,
                                                                                                              getPlayerName(player)))
    requestBallasGangSceneFinalCheck(scene, true)
end)

addEvent("tagup:ballasGangSceneFinalResult", true)
addEventHandler("tagup:ballasGangSceneFinalResult", resourceRoot, function(sceneId, result)
    local player = client
    local scene = mission.ballasGangScene
    if source ~= resourceRoot or not mission.running or mission.stage ~= "tags_ballas" or not isMissionPlayer(player) or not scene or
        scene.id ~= tonumber(sceneId) or not scene.finalCheckRequested or scene.finalReadyPlayers[player] then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas gang final camera result", 2)
        return
    end
    if result ~= "ready" then
        cancelBallasGangScene("client_camera_final_" .. tostring(result))
        return failMission("Un client a perdu la camera avant la fin de la mini-scene Ballas.")
    end
    scene.finalReadyPlayers[player] = true
    if allBallasGangScenePlayersReady(scene, "finalReadyPlayers") then
        releaseBallasGangSceneLeases(scene)
    end
end)

addEvent("tagup:ballasGangSceneReleased", true)
addEventHandler("tagup:ballasGangSceneReleased", resourceRoot, function(sceneId, result)
    local player = client
    local scene = mission.ballasGangScene
    if source ~= resourceRoot or not mission.running or mission.stage ~= "tags_ballas" or not isMissionPlayer(player) or not scene or
        scene.id ~= tonumber(sceneId) or not scene.releasing or scene.releasedPlayers[player] then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas gang release result", 2)
        return
    end
    if result ~= "released" then
        cancelBallasGangScene("client_camera_release_" .. tostring(result))
        return failMission("Un client n'a pas pu restaurer sa camera apres la mini-scene Ballas.")
    end
    scene.releasedPlayers[player] = true
    if allBallasGangScenePlayersReady(scene, "releasedPlayers") then
        completeBallasGangScene(scene)
    end
end)

addCommandHandler("tagup", function(player)
    if not player then
        return
    end
    startMission(player)
end)

addCommandHandler("tagupsweet1a", function(player)
    if not player then
        return
    end
    startMission(player)
end)

addCommandHandler("tagupballas", function(player)
    if not player then
        return
    end
    startMission(player, "ballas")
end)

addCommandHandler("tagupidlewood", function(player)
    if not player then
        return
    end
    startMission(player, "idlewood")
end)

addCommandHandler("tagupdeparture", function(player)
    if not player then
        return
    end
    startMission(player, "departure")
end)

addCommandHandler("taguppickup", function(player)
    if not player then
        return
    end
    startMission(player, "pickup")
end)

addCommandHandler("tagupfinal", function(player)
    if not player then
        return
    end
    startMission(player, "final")
end)

addCommandHandler("tagupabort", function(player)
    if mission.running and isMissionPlayer(player) then
        failMission("Mission abandonnee.")
    end
end)

addCommandHandler("taguplosespray", function(player)
    if not mission.running or mission.finishing or player ~= mission.leader then
        return
    end
    if not isSpraycanRequired() then
        outputChatBox("SWEET1 ne surveille plus le spraycan a cette phase.", player, 255, 190, 80)
        return
    end
    outputDebugString(("[tagging-up-turf] Forced SWE1SPF test requested by %s at stage=%s"):format(getPlayerName(player), mission.stage))
    takeWeapon(player, TAGUP.sprayWeapon)
end)

addCommandHandler("tagupskip", function(player)
    if not mission.running or player ~= mission.leader then
        return
    end
    if mission.stage == "sweet1a" and mission.fileCutscene then
        local scene = mission.fileCutscene
        if not scene.skipRequested then
            scene.skipRequested = true
            for _, member in ipairs(scene.players) do
                if isElement(member) then
                    triggerClientEvent(member, "tagup:fileCutsceneSkip", resourceRoot, scene.id)
                end
            end
        end
    elseif mission.stage == "intro" then
        cancelIntroScene("stage_skipped")
        for _, member in ipairs(mission.party) do
            setElementFrozen(member, false)
        end
        warpSweetIntoFirstFreeSeat()
        setStage("enter_car", {traceSkipped = true})
    elseif mission.stage == "enter_car" then
        if warpSweetIntoFirstFreeSeat() then
            setStage("drive_idlewood", {traceSkipped = true})
        end
    elseif mission.stage == "drive_idlewood" then
        setStage("demo", {traceSkipped = true})
        triggerEvent("tagup:beginDemo", resourceRoot)
    elseif mission.stage == "demo" then
        cancelDemoScene("stage_skipped")
        cancelDemoLeave("stage_skipped")
        cancelDemoWalk("stage_skipped")
        cancelDemoShoot("stage_skipped")
        cancelDemoEnter("stage_skipped")
        if isElement(mission.entities.sweet) then
            removePedFromVehicle(mission.entities.sweet)
        end
        -- Debug skips do not execute the native task trace. Seat Sweet immediately
        -- so later skip stages remain usable without weakening the real path.
        warpSweetIntoFirstFreeSeat()
        setStage("tags_idlewood", {traceSkipped = true})
    elseif mission.stage == "tags_ballas" and mission.ballasGangScene then
        if mission.ballasGangScene.skippable and not mission.ballasGangScene.finalCheckRequested then
            requestBallasGangSceneFinalCheck(mission.ballasGangScene, true)
        end
    elseif mission.stage == "tags_idlewood" or mission.stage == "tags_ballas" or mission.stage == "rooftop" then
        if mission.stage == "tags_ballas" then
            mission.ballasGangSceneCompleted = true
            if mission.ballasEncounter and mission.ballasEncounter.phase == "following" then
                startBallasAttack("stage_skipped")
            end
        end
        for _, id in ipairs(activeTagIds()) do
            mission.completedTags[id] = true
            mission.tagProgress[id] = 255
            replaceTagObject(id)
        end
        advanceAfterTags({traceSkipped = true})
    elseif mission.stage == "return_car" then
        setStage("drive_ballas", {traceSkipped = true})
    elseif mission.stage == "drive_ballas" then
        setStage("tags_ballas", {traceSkipped = true})
    elseif mission.stage == "ballas_departure" then
        cancelBallasDeparture("stage_skipped")
        for _, member in ipairs(mission.party) do
            removePedFromVehicle(member)
        end
        setStage("tags_ballas", {traceSkipped = true})
    elseif mission.stage == "return_after_roof" then
        setStage("drive_home", {traceSkipped = true})
    elseif mission.stage == "drive_home" then
        startFinalScene({traceSkipped = true})
    elseif mission.stage == "final_scene" and mission.finalScene then
        requestFinalSceneRelease(mission.finalScene, true)
    end
end)

local startDemoSequence

local function startDemoWalk(sweet, kind, overrideProfile)
    local profile = overrideProfile or TAGUP.sweetDemoWalk
    local exitX, exitY, exitZ = getElementPosition(sweet)
    local _, _, exitHeading = getElementRotation(sweet)
    local deltaX, deltaY = profile.target.x - exitX, profile.target.y - exitY
    local distance2D = math.sqrt(deltaX * deltaX + deltaY * deltaY)
    local distance3D = tagupDistance3D(exitX, exitY, exitZ, profile.target.x, profile.target.y, profile.target.z)

    -- SWEET1 starts this sequence only after its black-screen actor staging has
    -- placed Sweet at the exact scripted coordinate and heading.
    setElementSyncer(sweet, mission.leader)

    mission.demoWalkSerial = mission.demoWalkSerial + 1
    local walk = {id = mission.demoWalkSerial, ped = sweet, kind = kind or "spray", profile = profile}
    mission.demoWalk = walk
    walk.guardTimer = rememberTimer(setTimer(function(expectedId)
        if not mission.running or mission.stage ~= "demo" or not mission.demoWalk or mission.demoWalk.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native go-to #%d exceeded the %d ms server guard"):format(expectedId,
                                                                                                             profile.guardTimeout), 1)
        cancelDemoWalk("server_timeout")
        failMission("La marche native de Sweet a depasse le delai de garde.")
    end, profile.guardTimeout, 1, walk.id))

    local diagnostic =
        ("[tagging-up-turf] Starting Sweet native go-to #%d from SCM stage=(%.2f, %.2f, %.2f, heading=%.1f) to target=(%.2f, %.2f, %.2f), distance2D=%.2f m, distance3D=%.2f m, syncer=%s")
            :format(walk.id, exitX, exitY, exitZ, exitHeading, profile.target.x, profile.target.y, profile.target.z, distance2D, distance3D,
                    getPlayerName(mission.leader))
    outputDebugString(diagnostic)
    triggerClientEvent(mission.leader, "tagup:sweetDemoWalkStart", resourceRoot, walk.id, sweet, profile)
end

local function tryStartDemoSequenceAfterStaging()
    local scene = mission.demoScene
    local sweet = mission.entities.sweet
    if not scene or not scene.actorsStaged or not scene.sweetLeaveComplete or mission.demoShoot or not isElement(sweet) then
        return
    end
    startDemoSequence(sweet)
end

local function tryCompleteDemoLeave()
    local leave = mission.demoLeave
    if not leave or not isElement(leave.ped) or not leave.clientObserved or not leave.serverExited or getPedOccupiedVehicle(leave.ped) then
        return
    end

    local leaveId = leave.id
    outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d confirmed by task observation and server vehicle state"):format(leaveId))
    cancelDemoLeave("completed")
    local scene = mission.demoScene
    if scene then
        scene.sweetLeaveComplete = true
    end
    tryStartDemoSequenceAfterStaging()
end

local function startDemoLeave()
    if not mission.running or mission.stage ~= "demo" then
        return
    end
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) or not isElement(mission.leader) then
        return failMission("Sweet, la Greenwood ou le leader n'est plus disponible pour la demonstration.")
    end
    if getPedOccupiedVehicle(sweet) ~= vehicle then
        return failMission("Sweet n'est plus dans la Greenwood avant sa sortie native.")
    end

    cancelDemoLeave("replaced")
    cancelDemoWalk("replaced")
    cancelDemoShoot("replaced")
    setElementSyncer(sweet, mission.leader)

    mission.demoLeaveSerial = mission.demoLeaveSerial + 1
    local leave = {
        id = mission.demoLeaveSerial,
        ped = sweet,
        vehicle = vehicle,
        clientObserved = false,
        serverExited = false,
    }
    mission.demoLeave = leave
    leave.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoLeave
        if not mission.running or mission.stage ~= "demo" or not active or active.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d exceeded the %d ms server guard"):format(
                              expectedId, TAGUP.sweetDemoLeave.guardTimeout), 1)
        cancelDemoLeave("server_timeout")
        failMission("La sortie native de Sweet a depasse le delai de garde.")
    end, TAGUP.sweetDemoLeave.guardTimeout, 1, leave.id))

    outputDebugString(("[tagging-up-turf] Requesting Sweet native leave-car #%d from syncer %s"):format(leave.id, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:sweetDemoLeaveStart", resourceRoot, leave.id, sweet, vehicle, TAGUP.sweetDemoLeave)
end

local startSweetReturnEnter

local function allDemoScenePlayersReported(scene, field)
    for _, player in ipairs(scene.players) do
        if not isElement(player) or not scene[field][player] then
            return false
        end
    end
    return true
end

local function releaseDemoScene(scene, skipped)
    if mission.demoScene ~= scene or scene.releasing then
        return
    end
    scene.releasing = true
    scene.skipped = skipped == true
    scene.releasedPlayers = {}
    scene.releaseGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if active and active.id == expectedId then
            cancelDemoScene("release_timeout")
            failMission("La restauration de la camera de demonstration a depasse le delai de garde.")
        end
    end, TAGUP.sweetDemoScene.finalCheckTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoSceneRelease", resourceRoot, scene.id, scene.skipped)
        end
    end
end

local function finishDemoScene(scene)
    if mission.demoScene ~= scene then
        return
    end
    local skipped = scene.skipped
    local ped = mission.entities.sweet
    outputDebugString(("[tagging-up-turf] Sweet demonstration scene #%d %s after %d ms; camera/audio cleanup acknowledged by every participant")
                          :format(scene.id, skipped and "skipped" or "completed", getTickCount() - scene.startedAt))
    cancelDemoScene(skipped and "skipped" or "completed")

    -- SWEET1 can apply LOCKOUT_PLAYER_ONLY immediately after queuing this task
    -- because Sweet is an ordinary CPed in single-player. Synchronized MTA
    -- script peds use CPlayerPed internally, so the same lock aborts Sweet's
    -- running entry task. Defer it until the authoritative seat confirmation.
    mission.vehiclePlayerOnlyLocked = false
    setStage("tags_idlewood", {deferTraceStep = not skipped, traceSkipped = skipped})
    startTransitionAudio(TAGUP.transitionAudio.engineRunning, "engine_running")
    startSweetReturnEnter(ped)
end

local function requestDemoSceneFinalCheck(scene)
    if mission.demoScene ~= scene or scene.finalCheckRequested then
        return
    end
    scene.finalCheckRequested = true
    scene.finalReadyPlayers = {}
    scene.finalCheckGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if active and active.id == expectedId then
            cancelDemoScene("final_check_timeout")
            failMission("La verification finale de la camera de demonstration a depasse le delai de garde.")
        end
    end, TAGUP.sweetDemoScene.finalCheckTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoSceneFinalCheck", resourceRoot, scene.id)
        end
    end
end

local function playDemoCheckoutAudio(scene)
    if mission.demoScene ~= scene or scene.checkoutStarted then
        return
    end
    scene.checkoutStarted = true
    scene.checkoutFinishedPlayers = {}
    scene.audioGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if active and active.id == expectedId and not active.finalCheckRequested then
            cancelDemoScene("checkout_audio_timeout")
            failMission("La replique SWE1_CA n'a pas termine dans le delai attendu.")
        end
    end, TAGUP.sweetDemoScene.audioTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoScenePlayAudio", resourceRoot, scene.id, "checkout")
        end
    end
end

local function startDemoReturnWalk(ped)
    startDemoWalk(ped, "return", TAGUP.sweetDemoScene.sweetReturn)
end

local function startDemoCheckoutAnimation(scene)
    if mission.demoScene ~= scene or scene.checkoutAnimationStarted or not isElement(mission.entities.sweet) then
        return
    end
    scene.checkoutAnimationStarted = true
    local sweet = mission.entities.sweet
    setElementRotation(sweet, 0, 0, 280)
    if not setPedAnimation(sweet, "GRAFFITI", "graffiti_Chkout", -1, false, false, true, false, 250, false) then
        return failMission("GRAFFITI_CHKOUT a ete refusee par le pipeline d'animation synchronise.")
    end
    scene.animationGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if active and active.id == expectedId and not active.checkoutAnimationFinished then
            cancelDemoScene("checkout_animation_timeout")
            failMission("GRAFFITI_CHKOUT n'a pas termine dans le delai attendu.")
        end
    end, TAGUP.sweetDemoScene.animationTimeout, 1, scene.id))
    triggerClientEvent(mission.leader, "tagup:sweetDemoCheckoutObserve", resourceRoot, scene.id, sweet)
end

local function tryStartDemoSprayCamera(scene)
    if mission.demoScene ~= scene or scene.sprayCameraStarted or not scene.approachAudioFinished or not scene.shootObserved then
        return
    end
    scene.sprayCameraStarted = true
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoSceneSprayCamera", resourceRoot, scene.id)
        end
    end
end

local function stageDemoActors(scene)
    if mission.demoScene ~= scene or not isElement(mission.entities.sweet) then
        return
    end
    local profile = TAGUP.sweetDemoScene
    for index, player in ipairs(scene.players) do
        if isElement(player) then
            -- SWEET1 checks whether CJ is still in a vehicle only after the
            -- one-second fade has completed. Keep this authoritative fallback
            -- under black instead of visibly ejecting the player at +600 ms.
            if getPedOccupiedVehicle(player) then
                removePedFromVehicle(player)
            end
            local offset = profile.partyOffsets[index - 1]
            setElementPosition(player, profile.leaderStage.x + (offset and offset.x or 0), profile.leaderStage.y + (offset and offset.y or 0),
                               profile.leaderStage.z)
            setElementRotation(player, 0, 0, profile.leaderStage.heading)
        end
    end
    local sweet = mission.entities.sweet
    if getPedOccupiedVehicle(sweet) then
        removePedFromVehicle(sweet)
    end
    setElementPosition(sweet, profile.sweetStage.x, profile.sweetStage.y, profile.sweetStage.z)
    setElementRotation(sweet, 0, 0, profile.sweetStage.heading)
    setElementSyncer(sweet, mission.leader)
    scene.actorsStaged = true
    outputDebugString(("[tagging-up-turf] Sweet staged at SCM coordinate=(%.2f, %.2f, %.2f, heading=%.1f); sequence gate leaveComplete=%s"):format(
                          profile.sweetStage.x, profile.sweetStage.y, profile.sweetStage.z, profile.sweetStage.heading,
                          tostring(scene.sweetLeaveComplete)))
    tryStartDemoSequenceAfterStaging()
    for _, player in ipairs(scene.players) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:sweetDemoSceneStaged", resourceRoot, scene.id)
        end
    end
end

local function startDemoSceneTimeline(scene)
    if mission.demoScene ~= scene or scene.started or not allDemoScenePlayersReported(scene, "readyPlayers") then
        return
    end
    local profile = TAGUP.sweetDemoScene
    scene.started = true
    scene.startedAt = getTickCount()
    if isTimer(scene.readyGuardTimer) then
        killTimer(scene.readyGuardTimer)
    end
    for _, player in ipairs(scene.players) do
        triggerClientEvent(player, "tagup:sweetDemoSceneStart", resourceRoot, scene.id)
    end
    startDemoLeave()

    scene.playerExitTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if not active or active.id ~= expectedId then
            return
        end
        for _, player in ipairs(active.players) do
            if isElement(player) then
                -- The original assigns TASK_LEAVE_CAR to CJ 600 ms after
                -- Sweet. Each participant owns the equivalent local-player
                -- task; the later black-screen staging remains the fallback.
                triggerClientEvent(player, "tagup:sweetDemoPlayerExitStart", resourceRoot, active.id, mission.entities.vehicle)
            end
        end
    end, profile.sweetLeaveLead, 1, scene.id))
    scene.fadeTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if not active or active.id ~= expectedId then
            return
        end
        for _, player in ipairs(active.players) do
            if isElement(player) then
                triggerClientEvent(player, "tagup:sweetDemoSceneFadeOut", resourceRoot, active.id)
            end
        end
        active.stageTimer = rememberTimer(setTimer(function(stageId)
            local staging = mission.demoScene
            if not staging or staging.id ~= stageId then
                return
            end
            stageDemoActors(staging)
            staging.skipArmTimer = rememberTimer(setTimer(function(dialogueId)
                local dialogue = mission.demoScene
                if not dialogue or dialogue.id ~= dialogueId then
                    return
                end
                dialogue.skippable = true
                dialogue.approachFinishedPlayers = {}
                dialogue.audioGuardTimer = rememberTimer(setTimer(function(audioId)
                    local activeAudio = mission.demoScene
                    if activeAudio and activeAudio.id == audioId and not activeAudio.approachAudioFinished then
                        cancelDemoScene("approach_audio_timeout")
                        failMission("La replique SWE1_AR n'a pas termine dans le delai attendu.")
                    end
                end, TAGUP.sweetDemoScene.audioTimeout, 1, dialogue.id))
                for _, player in ipairs(dialogue.players) do
                    if isElement(player) then
                        triggerClientEvent(player, "tagup:sweetDemoSceneDialogue", resourceRoot, dialogue.id, player == mission.leader)
                    end
                end
            end, profile.blackHold + profile.skipArmDelay, 1, staging.id))
        end, profile.blackStageDelay, 1, active.id))
    end, profile.sweetLeaveLead + profile.fadeOutDelay, 1, scene.id))
end

local function beginDemoScene()
    if not mission.running or mission.stage ~= "demo" or mission.demoScene then
        return
    end
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) or not isElement(mission.leader) then
        return failMission("Sweet, la Greenwood ou le leader est indisponible avant la scene de demonstration.")
    end
    mission.demoSceneSerial = mission.demoSceneSerial + 1
    local scene = {id = mission.demoSceneSerial, players = {}, readyPlayers = {}, requestedAt = getTickCount()}
    mission.demoScene = scene
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            table.insert(scene.players, player)
        end
    end
    scene.readyGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoScene
        if active and active.id == expectedId and not active.started then
            cancelDemoScene("ready_timeout")
            failMission("La camera ou l'audio de demonstration n'est pas pret sur tous les clients.")
        end
    end, TAGUP.sweetDemoScene.readyTimeout, 1, scene.id))
    for _, player in ipairs(scene.players) do
        triggerClientEvent(player, "tagup:sweetDemoScenePrepare", resourceRoot, scene.id, sweet)
    end
end

addEvent("tagup:beginDemo", false)
addEventHandler("tagup:beginDemo", resourceRoot, beginDemoScene)

addEvent("tagup:sweetDemoSceneReady", true)
addEventHandler("tagup:sweetDemoSceneReady", resourceRoot, function(sceneId, result, details)
    local player, scene = client, mission.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) or scene.started or scene.readyPlayers[player] then
        return
    end
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player=%s ready=%s (%s)"):format(
                          scene.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelDemoScene("client_prepare_" .. tostring(result))
        return failMission("La preparation native de la demonstration a echoue: " .. tostring(result))
    end
    scene.readyPlayers[player] = true
    startDemoSceneTimeline(scene)
end)

addEvent("tagup:sweetDemoSceneLeaseLost", true)
addEventHandler("tagup:sweetDemoSceneLeaseLost", resourceRoot, function(sceneId)
    local scene = mission.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(client) then
        return
    end
    outputDebugString(("[tagging-up-turf] Sweet demo scene #%d lease lost on %s"):format(scene.id, getPlayerName(client)), 2)
    cancelDemoScene("camera_lease_lost")
    failMission("Un client a perdu la camera native pendant la demonstration de Sweet.")
end)

addEvent("tagup:sweetDemoSceneAudioFinished", true)
addEventHandler("tagup:sweetDemoSceneAudioFinished", resourceRoot, function(sceneId, cue, result)
    local player, scene = client, mission.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not isMissionPlayer(player) then
        return
    end
    if result ~= "finished" then
        outputDebugString(("[tagging-up-turf] Sweet demo scene #%d player=%s cue=%s failed=%s"):format(
                              scene.id, getPlayerName(player), tostring(cue), tostring(result)), 2)
        cancelDemoScene("client_audio_" .. tostring(result))
        return failMission("Le dialogue natif de Sweet a echoue: " .. tostring(result))
    end
    local field = cue == "approach" and "approachFinishedPlayers" or cue == "checkout" and "checkoutFinishedPlayers" or nil
    if not field or not scene[field] or scene[field][player] then
        return
    end
    scene[field][player] = true
    if not allDemoScenePlayersReported(scene, field) then
        return
    end
    if cue == "approach" then
        if isTimer(scene.audioGuardTimer) then
            killTimer(scene.audioGuardTimer)
            scene.audioGuardTimer = nil
        end
        scene.approachAudioFinished = true
        tryStartDemoSprayCamera(scene)
    else
        if isTimer(scene.audioGuardTimer) then
            killTimer(scene.audioGuardTimer)
            scene.audioGuardTimer = nil
        end
        local profile = TAGUP.sweetDemoScene
        for index, member in ipairs(scene.players) do
            if isElement(member) then
                local offset = profile.partyOffsets[index - 1]
                setElementPosition(member, profile.leaderFinal.x + (offset and offset.x or 0),
                                   profile.leaderFinal.y + (offset and offset.y or 0), profile.leaderFinal.z)
                setElementRotation(member, 0, 0, profile.leaderFinal.heading)
            end
        end
        requestDemoSceneFinalCheck(scene)
    end
end)

addEvent("tagup:sweetDemoCheckoutResult", true)
addEventHandler("tagup:sweetDemoCheckoutResult", resourceRoot, function(sceneId, ped, result, details)
    local scene = mission.demoScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or ped ~= mission.entities.sweet or
        not scene.checkoutAnimationStarted or scene.checkoutAnimationFinished then
        return
    end
    if result ~= "finished" then
        cancelDemoScene("checkout_" .. tostring(result))
        return failMission("GRAFFITI_CHKOUT a ete interrompue: " .. tostring(details or result))
    end
    scene.checkoutAnimationFinished = true
    if isTimer(scene.animationGuardTimer) then
        killTimer(scene.animationGuardTimer)
    end
    setPedAnimation(ped, false)
    startDemoReturnWalk(ped)
end)

addEvent("tagup:sweetDemoSceneFinalResult", true)
addEventHandler("tagup:sweetDemoSceneFinalResult", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not scene.finalCheckRequested or
        not isMissionPlayer(player) or scene.finalReadyPlayers[player] then
        return
    end
    if result ~= "ready" then
        cancelDemoScene("camera_final_" .. tostring(result))
        return failMission("Un client a perdu la camera avant la fin de la demonstration.")
    end
    scene.finalReadyPlayers[player] = true
    if allDemoScenePlayersReported(scene, "finalReadyPlayers") then
        releaseDemoScene(scene, false)
    end
end)

addEvent("tagup:sweetDemoSceneReleased", true)
addEventHandler("tagup:sweetDemoSceneReleased", resourceRoot, function(sceneId, result)
    local player, scene = client, mission.demoScene
    if source ~= resourceRoot or not scene or scene.id ~= tonumber(sceneId) or not scene.releasing or not isMissionPlayer(player) or
        scene.releasedPlayers[player] then
        return
    end
    if result ~= "released" then
        cancelDemoScene("release_" .. tostring(result))
        return failMission("Un client n'a pas restaure la demonstration correctement.")
    end
    scene.releasedPlayers[player] = true
    if allDemoScenePlayersReported(scene, "releasedPlayers") then
        finishDemoScene(scene)
    end
end)

addEvent("tagup:sweetDemoSceneSkipRequest", true)
addEventHandler("tagup:sweetDemoSceneSkipRequest", resourceRoot, function(sceneId)
    local scene = mission.demoScene
    if source ~= resourceRoot or client ~= mission.leader or not scene or scene.id ~= tonumber(sceneId) or not scene.skippable or scene.releasing then
        return
    end
    scene.skippable = false
    scene.skipped = true
    cancelDemoLeave("scene_skipped")
    cancelDemoWalk("scene_skipped")
    cancelDemoShoot("scene_skipped")
    setPedAnimation(mission.entities.sweet, false)
    setElementData(mission.entities.demoTag, "tagup.paintAlpha", 255, true)
    local profile = TAGUP.sweetDemoScene
    for index, player in ipairs(scene.players) do
        if isElement(player) then
            local offset = profile.partyOffsets[index - 1]
            setElementPosition(player, profile.leaderFinal.x + (offset and offset.x or 0), profile.leaderFinal.y + (offset and offset.y or 0),
                               profile.leaderFinal.z)
            setElementRotation(player, 0, 0, profile.leaderFinal.heading)
        end
    end
    setElementPosition(mission.entities.sweet, profile.sweetFinal.x, profile.sweetFinal.y, profile.sweetFinal.z)
    setElementRotation(mission.entities.sweet, 0, 0, profile.sweetFinal.heading)
    releaseDemoScene(scene, true)
end)

addEvent("tagup:sweetDemoLeaveResult", true)
addEventHandler("tagup:sweetDemoLeaveResult", resourceRoot, function(leaveId, ped, vehicle, result, details)
    local player = client
    local leave = mission.demoLeave
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not leave or leave.id ~= tonumber(leaveId) or leave.ped ~= ped or leave.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle or not isElement(ped) or not isElement(vehicle) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet leave-car result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native leave-car #%d result=%s (%s)"):format(leave.id, tostring(result), details:sub(1, 240)))
    if result ~= "exited" then
        cancelDemoLeave("client_" .. tostring(result))
        return failMission("La sortie native de Sweet a echoue: " .. tostring(result))
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoLeave("invalid_syncer")
        return failMission("Le resultat de sortie de Sweet ne vient plus de son syncer.")
    end

    leave.clientObserved = true
    if getPedOccupiedVehicle(ped) ~= vehicle then
        leave.serverExited = true
    end
    tryCompleteDemoLeave()
end)

startDemoSequence = function(ped)
    local profile = TAGUP.sweetDemoShoot
    local demo = TAGUP.demoTag
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) then
        return failMission("La Greenwood a disparu avant la sequence native de demonstration.")
    end
    giveWeapon(ped, TAGUP.sprayWeapon, 500, true)
    setElementSyncer(ped, mission.leader)

    mission.demoShootSerial = mission.demoShootSerial + 1
    local shoot = {id = mission.demoShootSerial, ped = ped, requestedAt = getTickCount()}
    mission.demoShoot = shoot
    shoot.guardTimer = rememberTimer(setTimer(function(expectedId)
        if not mission.running or mission.stage ~= "demo" or not mission.demoShoot or mission.demoShoot.id ~= expectedId then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native sequence #%d exceeded the %d ms server guard"):format(
                              expectedId, profile.sequenceGuardTimeout), 1)
        cancelDemoShoot("server_timeout")
        failMission("La sequence native de demonstration de Sweet a depasse le delai de garde.")
    end, profile.sequenceGuardTimeout, 1, shoot.id))

    outputDebugString(("[tagging-up-turf] Starting Sweet native sequence #%d: leave_car -> walk -> shoot (shoot duration=%d, burst=%d)"):format(
                          shoot.id, profile.duration, profile.burstLength))
    triggerClientEvent(mission.leader, "tagup:sweetDemoSequenceStart", resourceRoot, shoot.id, ped, vehicle, demo, TAGUP.sweetDemoWalk, profile)
end

local function tryCompleteSweetReturnEnter()
    local enter = mission.demoEnter
    if not enter or not enter.clientObserved or not enter.serverEntered or not isElement(enter.ped) or not isElement(enter.vehicle) or
        getPedOccupiedVehicle(enter.ped) ~= enter.vehicle or getPedOccupiedVehicleSeat(enter.ped) ~= enter.seat then
        return
    end

    outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d confirmed by task observation and server vehicle state (seat=%d)"):format(
                          enter.id, enter.seat))
    cancelDemoEnter("completed")
    mission.vehiclePlayerOnlyLocked = true
    outputDebugString("[tagging-up-turf] Greenwood LOCKOUT_PLAYER_ONLY armed after Sweet entered the passenger seat")
    broadcastState({message = "Sweet est remonte dans la Greenwood."})
end

startSweetReturnEnter = function(ped)
    local vehicle = mission.entities.vehicle
    local profile = TAGUP.sweetReturnEnter
    if not isElement(ped) or not isElement(vehicle) or not isElement(mission.leader) then
        return failMission("Sweet, la Greenwood ou le leader a disparu avant l'entree passager native.")
    end
    if getPedOccupiedVehicle(ped) then
        return failMission("Sweet occupe deja un vehicule avant l'entree passager native.")
    end
    if getVehicleOccupant(vehicle, profile.seat) then
        return failMission("Le siege passager de Sweet est deja occupe.")
    end

    cancelDemoEnter("replaced")
    setElementSyncer(ped, mission.leader)
    mission.demoEnterSerial = mission.demoEnterSerial + 1
    local enter = {
        id = mission.demoEnterSerial,
        ped = ped,
        vehicle = vehicle,
        seat = profile.seat,
        requestedAt = getTickCount(),
        clientObserved = false,
        serverEntered = false,
    }
    mission.demoEnter = enter
    enter.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.demoEnter
        if not mission.running or not active or active.id ~= expectedId or
            (mission.stage ~= "tags_idlewood" and mission.stage ~= "return_car") then
            return
        end
        outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d exceeded the %d ms server guard"):format(
                              expectedId, profile.guardTimeout), 1)
        cancelDemoEnter("server_timeout")
        failMission("L'entree passager native de Sweet a depasse le delai de garde.")
    end, profile.guardTimeout, 1, enter.id))

    outputDebugString(("[tagging-up-turf] Requesting Sweet native passenger entry #%d (SCM seat=0, MTA seat=%d, SCM timeout=%d ms) from syncer %s"):format(
                          enter.id, enter.seat, profile.scmTimeout, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:sweetReturnEnterStart", resourceRoot, enter.id, ped, vehicle, profile)
end

addEvent("tagup:sweetDemoWalkResult", true)
addEventHandler("tagup:sweetDemoWalkResult", resourceRoot, function(walkId, ped, result, details)
    local player = client
    local walk = mission.demoWalk
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not walk or walk.id ~= tonumber(walkId) or walk.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet go-to result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native go-to #%d result=%s (%s)"):format(walk.id, tostring(result), details:sub(1, 240)))
    if result ~= "arrived" and result ~= "timeout_relocated" then
        cancelDemoWalk("client_" .. tostring(result))
        return failMission("La marche native de Sweet a echoue: " .. tostring(result))
    end

    local x, y = getElementPosition(ped)
    local profile = walk.profile or TAGUP.sweetDemoWalk
    local target = profile.target
    local distance = getDistanceBetweenPoints2D(x, y, target.x, target.y)
    if distance > profile.serverCompletionRadius then
        cancelDemoWalk("invalid_completion_position")
        return failMission(("Sweet a termine sa marche trop loin du tag (%.2f m)."):format(distance))
    end

    local kind = walk.kind
    cancelDemoWalk("completed")
    if kind == "return" then
        local scene = mission.demoScene
        if not scene then
            return
        end
        setElementRotation(ped, 0, 0, TAGUP.sweetDemoScene.sweetFinal.heading)
        playDemoCheckoutAudio(scene)
    else
        failMission("Une marche de demonstration non sequentielle a ete lancee par erreur.")
    end
end)

addEvent("tagup:sweetDemoSequenceResult", true)
addEventHandler("tagup:sweetDemoSequenceResult", resourceRoot, function(shootId, ped, result, details)
    local player = client
    local shoot = mission.demoShoot
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not shoot or shoot.id ~= tonumber(shootId) or shoot.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) or isPedDead(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet shoot result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native sequence #%d result=%s (%s)"):format(shoot.id, tostring(result), details:sub(1, 240)))
    if getElementSyncer(ped) ~= player then
        cancelDemoShoot("invalid_syncer")
        return failMission("Le resultat du tir de Sweet ne vient plus de son syncer.")
    end

    -- In SWEET1 the 15-second task is only a ceiling: CTagManager reaching 100%
    -- interrupts it. Any spontaneous task end before our authoritative tag reaches
    -- 100% is therefore a failure, not the success condition for this stage.
    cancelDemoShoot("client_" .. tostring(result))
    failMission("La sequence native de Sweet s'est terminee avant que le tag soit recouvert: " .. tostring(result))
end)

addEvent("tagup:sweetDemoSequenceShootObserved", true)
addEventHandler("tagup:sweetDemoSequenceShootObserved", resourceRoot, function(shootId, ped)
    local player = client
    local shoot = mission.demoShoot
    if source ~= resourceRoot or not mission.running or mission.stage ~= "demo" or player ~= mission.leader or not isMissionPlayer(player) or
        not shoot or shoot.id ~= tonumber(shootId) or shoot.ped ~= ped or ped ~= mission.entities.sweet or not isElement(ped) or isPedDead(ped) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet shoot observation", 2)
        return
    end
    if shoot.observedAt then
        outputDebugString(("[tagging-up-turf] Ignoring duplicate Sweet native shoot #%d observation"):format(shoot.id), 2)
        return
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoShoot("invalid_observation_syncer")
        return failMission("Le demarrage du tir de Sweet ne vient pas de son syncer.")
    end

    local profile = TAGUP.sweetDemoShoot
    local demo = TAGUP.demoTag
    local x, y, z = getElementPosition(ped)
    local distance = tagupDistance3D(x, y, z, demo.x, demo.y, demo.z)
    if distance > profile.serverMaxDistance or getPedWeapon(ped) ~= TAGUP.sprayWeapon or not isElement(mission.entities.demoTag) then
        cancelDemoShoot("invalid_observation_state")
        return failMission(("Sweet a commence le tir dans un etat invalide (distance=%.2f m, weapon=%d)."):format(distance, getPedWeapon(ped)))
    end

    shoot.observedAt = getTickCount()
    -- A native spray hit can be emitted in the frame before the Lua task
    -- observer reports TASK_SIMPLE_GUN_CTRL. Preserve that stronger evidence
    -- instead of rolling the authoritative byte back to zero.
    shoot.progress = shoot.progress or 0
    local demoScene = mission.demoScene
    if demoScene then
        demoScene.shootObserved = true
        tryStartDemoSprayCamera(demoScene)
    end
    outputDebugString(("[tagging-up-turf] Sweet native sequence #%d reached shoot task after %d ms; waiting for native tag hits (task ceiling=%d ms)"):format(
        shoot.id, shoot.observedAt - shoot.requestedAt, profile.duration))
end)

local function completeDemoTag(active)
    if not active or active.nativeCancelled then
        return
    end

    local profile = TAGUP.sweetDemoShoot
    active.nativeCancelled = true
    setElementData(mission.entities.demoTag, "tagup.paintAlpha", 255, true)
    triggerClientEvent(mission.leader, "tagup:sweetDemoSequenceCancel", resourceRoot, active.id, "authoritative_tag_complete")
    outputDebugString(("[tagging-up-turf] Sweet demo tag reached native alpha 255; honoring SCM WAIT %d"):format(profile.postCompletionWait))

    active.completionTimer = rememberTimer(setTimer(function(completedId)
        local completed = mission.demoShoot
        if not mission.running or mission.stage ~= "demo" or not completed or completed.id ~= completedId then
            return
        end
        if not isElement(completed.ped) or isPedDead(completed.ped) or not isElement(mission.entities.demoTag) then
            cancelDemoShoot("invalid_post_wait_state")
            return failMission("Sweet ou le tag a disparu pendant l'attente de fin de demonstration.")
        end

        local scene = mission.demoScene
        cancelDemoShoot("completed_after_scm_wait")
        if not scene then
            return failMission("La scene de demonstration a disparu avant GRAFFITI_CHKOUT.")
        end
        outputDebugString("[tagging-up-turf] SCM WAIT 1000 complete; starting synchronized GRAFFITI_CHKOUT")
        startDemoCheckoutAnimation(scene)
    end, profile.postCompletionWait, 1, active.id))
end

local function allMissionPlayersExitedBallasVehicle(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.exitedPlayers[player] then
            return false
        end
    end
    return true
end

local function allMissionPlayersFinishedBallasAudio(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.audioFinishedPlayers[player] then
            return false
        end
    end
    return true
end

local function allMissionPlayersReadyForBallasAudio(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.audioReadyPlayers[player] then
            return false
        end
    end
    return true
end

local function allMissionPlayersCameraReady(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.cameraReadyPlayers[player] then
            return false
        end
    end
    return true
end

local function allMissionPlayersCameraFinalReady(departure)
    for _, player in ipairs(mission.party) do
        if isElement(player) and not departure.cameraFinalReadyPlayers[player] then
            return false
        end
    end
    return true
end

local function requestBallasCameraFinalCheck(departure)
    if departure.finalCheckRequested or not departure.wanderObserved or not departure.postWaitElapsed then
        return
    end

    departure.finalCheckRequested = true
    departure.cameraFinalReadyPlayers = {}
    departure.finalCheckGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasDeparture("camera_final_check_timeout")
        failMission("La verification finale de la camera Ballas a depasse le delai de garde.")
    end, TAGUP.ballasDeparture.camera.finalCheckTimeout, 1, departure.id))

    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasCameraFinalCheck", resourceRoot, departure.id, departure.vehicle)
        end
    end
end

local function startBallasDepartureScene(departure)
    if departure.cameraStarted or not allMissionPlayersCameraReady(departure) then
        return
    end

    -- Cameras are local state. This barrier gives every participant the same
    -- authoritative scene boundary before any native leave-car task can start.
    departure.cameraStarted = true
    departure.requestedAt = getTickCount()
    if isTimer(departure.cameraGuardTimer) then
        killTimer(departure.cameraGuardTimer)
    end
    departure.guardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasDeparture("server_timeout")
        failMission("La sequence native de depart de Sweet a depasse le delai de garde.")
    end, TAGUP.ballasDeparture.guardTimeout, 1, departure.id))

    outputDebugString(("[tagging-up-turf] Ballas camera barrier #%d passed for %d participant(s); starting native exits and post-task audio load gate"):format(
                          departure.id, #mission.party))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasPlayerExitStart", resourceRoot, departure.id, departure.vehicle)
        end
    end
end

local function tryStartBallasAudio(departure)
    if not departure or departure.audioStarted or not allMissionPlayersReadyForBallasAudio(departure) then
        return
    end

    departure.audioStarted = true
    outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d load barrier passed for %d participant(s); starting native playback"):format(
                          departure.id, #mission.party))
    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasAudioStart", resourceRoot, departure.id, departure.vehicle)
        end
    end
end

local function tryStartBallasWander()
    local departure = mission.ballasDeparture
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not departure or departure.wanderRequested or not allMissionPlayersExitedBallasVehicle(departure) or
        not allMissionPlayersFinishedBallasAudio(departure) then
        return
    end
    if not isElement(sweet) or not isElement(vehicle) or getVehicleController(vehicle) or getPedOccupiedVehicle(sweet) ~= vehicle then
        cancelBallasDeparture("invalid_wander_state")
        return failMission("La Greenwood n'est pas prete pour le depart natif de Sweet.")
    end

    -- The ped task drives the vehicle autopilot, so the same persistent client
    -- must own both streams for ordinary MTA synchronization to carry 05D2.
    setElementSyncer(sweet, mission.leader, true, true)
    setElementSyncer(vehicle, mission.leader, true, true)
    departure.wanderRequested = true
    departure.wanderRequestedAt = getTickCount()
    outputDebugString(("[tagging-up-turf] All players exited and SWE1_AV finished; requesting 05D2 DriveWander #%d (speed=%.1f, style=%s) from %s"):format(
                          departure.id, TAGUP.ballasDeparture.speed, TAGUP.ballasDeparture.drivingStyle, getPlayerName(mission.leader)))
    triggerClientEvent(mission.leader, "tagup:ballasDriveWanderStart", resourceRoot, departure.id, sweet, vehicle, TAGUP.ballasDeparture)
end

local function startBallasDeparture()
    local vehicle, sweet = mission.entities.vehicle, mission.entities.sweet
    if not isElement(vehicle) or not isElement(sweet) then
        return failMission("Sweet ou la Greenwood a disparu a l'arrivee Ballas.")
    end
    mission.vehiclePlayerOnlyLocked = true

    cancelBallasDeparture("replaced")
    mission.ballasDepartureSerial = mission.ballasDepartureSerial + 1
    local departure = {
        id = mission.ballasDepartureSerial,
        vehicle = vehicle,
        ped = sweet,
        exitedPlayers = {},
        clientExitReports = {},
        cameraReadyPlayers = {},
        audioReadyPlayers = {},
        audioFinishedPlayers = {},
        requestedAt = getTickCount(),
    }
    mission.ballasDeparture = departure
    setStage("ballas_departure", {deferTraceStep = true})

    departure.cameraGuardTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        cancelBallasDeparture("camera_ready_timeout")
        failMission("La camera d'arrivee Ballas n'est pas prete sur tous les clients.")
    end, TAGUP.ballasDeparture.camera.readyTimeout, 1, departure.id))

    for _, player in ipairs(mission.party) do
        if isElement(player) then
            triggerClientEvent(player, "tagup:ballasCameraPrepare", resourceRoot, departure.id, vehicle)
        end
    end
end

addEvent("tagup:ballasCameraReady", true)
addEventHandler("tagup:ballasCameraReady", resourceRoot, function(departureId, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or departure.cameraStarted or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or not isMissionPlayer(player) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas camera result", 2)
        return
    end
    if departure.cameraReadyPlayers[player] then
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas camera #%d player=%s result=%s (%s)"):format(
                          departure.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelBallasDeparture("client_camera_" .. tostring(result))
        return failMission("La camera native d'un membre de l'equipe a echoue: " .. tostring(result))
    end
    departure.cameraReadyPlayers[player] = true
    startBallasDepartureScene(departure)
end)

addEvent("tagup:ballasCameraLeaseLost", true)
addEventHandler("tagup:ballasCameraLeaseLost", resourceRoot, function(departureId, vehicle)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or not isMissionPlayer(player) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas camera lease-loss report", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas camera #%d lease lost on %s"):format(departure.id, getPlayerName(player)), 2)
    cancelBallasDeparture("client_camera_lease_lost")
    failMission("Un client a perdu le controle de la camera native pendant la scene Ballas.")
end)

addEvent("tagup:ballasAudioReady", true)
addEventHandler("tagup:ballasAudioReady", resourceRoot, function(departureId, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or not departure.cameraStarted or
        departure.audioStarted or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or
        not isMissionPlayer(player) or departure.audioReadyPlayers[player] then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas SWE1_AV ready result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d player=%s ready=%s (%s)"):format(
                          departure.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "ready" then
        cancelBallasDeparture("client_audio_ready_" .. tostring(result))
        return failMission("Le chargement de SWE1_AV a echoue sur un client: " .. tostring(result))
    end

    departure.audioReadyPlayers[player] = true
    tryStartBallasAudio(departure)
end)

addEvent("tagup:ballasAudioResult", true)
addEventHandler("tagup:ballasAudioResult", resourceRoot, function(departureId, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or not departure.audioStarted or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or
        not isMissionPlayer(player) or not departure.audioReadyPlayers[player] or departure.audioFinishedPlayers[player] then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas SWE1_AV result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas SWE1_AV #%d player=%s result=%s (%s)"):format(
                          departure.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "finished" then
        cancelBallasDeparture("client_audio_" .. tostring(result))
        return failMission("La replique SWE1_AV a echoue sur un client: " .. tostring(result))
    end

    departure.audioFinishedPlayers[player] = true
    tryStartBallasWander()
end)

addEvent("tagup:ballasCameraFinalResult", true)
addEventHandler("tagup:ballasCameraFinalResult", resourceRoot, function(departureId, vehicle, result)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or not departure.finalCheckRequested or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or not isMissionPlayer(player) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas final camera result", 2)
        return
    end
    if departure.cameraFinalReadyPlayers[player] then
        return
    end
    if result ~= "ready" then
        cancelBallasDeparture("client_camera_final_" .. tostring(result))
        return failMission("Un client a perdu la camera native avant la fin de la scene Ballas.")
    end

    departure.cameraFinalReadyPlayers[player] = true
    if allMissionPlayersCameraFinalReady(departure) then
        cancelBallasDeparture("keep_wandering")
        setStage("tags_ballas")
    end
end)

addEvent("tagup:ballasPlayerExitResult", true)
addEventHandler("tagup:ballasPlayerExitResult", resourceRoot, function(departureId, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or not departure or not departure.cameraStarted or
        departure.id ~= tonumber(departureId) or departure.vehicle ~= vehicle or vehicle ~= mission.entities.vehicle or not isMissionPlayer(player) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Ballas exit result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Ballas departure #%d player=%s exit=%s (%s)"):format(
                          departure.id, getPlayerName(player), tostring(result), tostring(details or ""):sub(1, 180)))
    if result ~= "exited" and result ~= "already_out" then
        cancelBallasDeparture("client_exit_" .. tostring(result))
        return failMission("La sortie native d'un membre de l'equipe a echoue: " .. tostring(result))
    end
    departure.clientExitReports[player] = true
    if getPedOccupiedVehicle(player) ~= vehicle then
        departure.exitedPlayers[player] = true
        tryStartBallasWander()
    else
        outputDebugString(("[tagging-up-turf] Waiting for server onVehicleExit for %s"):format(getPlayerName(player)))
    end
end)

addEvent("tagup:ballasDriveWanderResult", true)
addEventHandler("tagup:ballasDriveWanderResult", resourceRoot, function(departureId, ped, vehicle, result, details)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or player ~= mission.leader or not departure or
        departure.id ~= tonumber(departureId) or departure.ped ~= ped or departure.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized DriveWander result", 2)
        return
    end

    outputDebugString(("[tagging-up-turf] Sweet 05D2 #%d result=%s (%s)"):format(
                          departure.id, tostring(result), tostring(details or ""):sub(1, 220)))
    if result ~= "observed" then
        cancelBallasDeparture("client_wander_" .. tostring(result))
        return failMission("Le depart natif de Sweet a echoue: " .. tostring(result))
    end
    if not departure.wanderAccepted or departure.wanderObserved or getElementSyncer(ped) ~= player or getElementSyncer(vehicle) ~= player or
        getVehicleController(vehicle) then
        cancelBallasDeparture("invalid_wander_observation")
        return failMission("L'observation 05D2 ne vient pas du double syncer attendu.")
    end

    departure.wanderObserved = true
    if departure.postWaitElapsed then
        requestBallasCameraFinalCheck(departure)
    end
end)

addEvent("tagup:ballasDriveWanderAccepted", true)
addEventHandler("tagup:ballasDriveWanderAccepted", resourceRoot, function(departureId, ped, vehicle)
    local player = client
    local departure = mission.ballasDeparture
    if source ~= resourceRoot or not mission.running or mission.stage ~= "ballas_departure" or player ~= mission.leader or not departure or
        departure.id ~= tonumber(departureId) or departure.ped ~= ped or departure.vehicle ~= vehicle or ped ~= mission.entities.sweet or
        vehicle ~= mission.entities.vehicle or not departure.wanderRequested or departure.wanderAccepted or getElementSyncer(ped) ~= player or
        getElementSyncer(vehicle) ~= player or getVehicleController(vehicle) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized DriveWander acceptance", 2)
        return
    end

    departure.wanderAccepted = true
    -- SWEET1 resets TIMERA immediately before the WAIT 1000 that follows 05D2.
    mission.ballasTimerResetAt = getTickCount()
    departure.postStartTimer = rememberTimer(setTimer(function(expectedId)
        local active = mission.ballasDeparture
        if not mission.running or mission.stage ~= "ballas_departure" or not active or active.id ~= expectedId then
            return
        end
        active.postWaitElapsed = true
        if active.wanderObserved then
            requestBallasCameraFinalCheck(active)
        end
    end, TAGUP.ballasDeparture.postStartWait, 1, departure.id))
end)

local function isFiniteNumber(value)
    return type(value) == "number" and value == value and math.abs(value) < math.huge
end

local function validateReportedVehicleArrival(kind, vehicle, target, gate, reportedX, reportedY, reportedZ)
    if not isFiniteNumber(reportedX) or not isFiniteNumber(reportedY) or not isFiniteNumber(reportedZ) then
        return false
    end
    if math.abs(reportedX - target[1]) > gate.radiusX or math.abs(reportedY - target[2]) > gate.radiusY or
        math.abs(reportedZ - target[3]) > gate.radiusZ then
        return false
    end

    local serverX, serverY, serverZ = getElementPosition(vehicle)
    local deltaX, deltaY, deltaZ = serverX - reportedX, serverY - reportedY, serverZ - reportedZ
    local serverDrift = math.sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ)
    -- The current vehicle syncer already owns the transform sent to MTA. This
    -- generous sanity envelope rejects unrelated/stale reports without racing
    -- the exact SCM box against a transform received in another packet.
    if serverDrift > 40 then
        if getTickCount() - (mission.lastArrivalRejectLog or 0) >= 1000 then
            mission.lastArrivalRejectLog = getTickCount()
            outputDebugString(("[tagging-up-turf] Deferred %s LOCATE_CAR_3D report: server drift %.2f m"):format(tostring(kind), serverDrift), 2)
        end
        return false
    end

    outputDebugString(("[tagging-up-turf] Accepted %s LOCATE_CAR_3D report=(%.2f, %.2f, %.2f), server drift=%.2f m"):format(
                          tostring(kind), reportedX, reportedY, reportedZ, serverDrift))
    return true
end

addEvent("tagup:vehicleReady", true)
addEventHandler("tagup:vehicleReady", resourceRoot, function(kind, reportedX, reportedY, reportedZ)
    local player = client
    if not mission.running or not isMissionPlayer(player) or player ~= mission.leader then
        return
    end
    local vehicle = mission.entities.vehicle
    if getPedOccupiedVehicle(player) ~= vehicle or getVehicleController(vehicle) ~= player then
        return
    end

    if kind == "party" and mission.stage == "enter_car" and isPartyInVehicle() then
        local sweet = mission.entities.sweet
        if isElement(sweet) and getPedOccupiedVehicle(sweet) == vehicle and getPedOccupiedVehicleSeat(sweet) == 1 then
            mission.introEntryPending = false
            setStage("drive_idlewood", {message = "Sweet est a bord. Direction Idlewood."})
        elseif mission.introEntryPending then
            broadcastState({message = "Attendez que Sweet finisse de monter."})
        elseif warpSweetIntoFirstFreeSeat() then
            -- Debug checkpoints and /tagupskip do not execute the intro task.
            setStage("drive_idlewood", {message = "Sweet est a bord. Direction Idlewood."})
        else
            broadcastState({message = "Impossible d'installer Sweet dans la voiture."})
        end
    elseif kind == "idlewood" and mission.stage == "drive_idlewood" then
        local target, gate = TAGUP.idlewoodDestination, TAGUP.idlewoodArrival
        if validateReportedVehicleArrival(kind, vehicle, target, gate, reportedX, reportedY, reportedZ) then
            -- The camera lease inhibits controls immediately. Neon mirrors the
            -- native bPlayerSafe pad flag, so GTA brakes the synchronized car
            -- exactly as it does after SWEET1 SET_PLAYER_CONTROL OFF.
            setStage("demo")
            triggerEvent("tagup:beginDemo", resourceRoot)
        end
    elseif kind == "returned" and mission.stage == "return_car" and isPartyInVehicle() then
        if getPedOccupiedVehicle(mission.entities.sweet) == vehicle and getPedOccupiedVehicleSeat(mission.entities.sweet) == TAGUP.sweetReturnEnter.seat then
            if not mission.transitionAudio then
                startTransitionAudio(TAGUP.transitionAudio.ballasDeparture, "ballas_departure", function()
                    if mission.running and mission.stage == "return_car" and isPartyInVehicle() then
                        setStage("drive_ballas")
                    end
                end)
            end
        elseif mission.demoEnter then
            broadcastState({message = "Attendez que Sweet finisse de monter."})
        else
            failMission("Sweet n'est pas dans la Greenwood apres son entree passager native.")
        end
    elseif kind == "ballas" and mission.stage == "drive_ballas" then
        local target, gate = TAGUP.ballasDestination, TAGUP.ballasArrival
        if validateReportedVehicleArrival(kind, vehicle, target, gate, reportedX, reportedY, reportedZ) then
            startBallasDeparture()
        end
    elseif kind == "roof_return" and mission.stage == "return_after_roof" and isPartyInVehicle() then
        warpSweetIntoFirstFreeSeat()
        setStage("drive_home")
        startTransitionAudio(TAGUP.transitionAudio.groveReturn, "grove_return")
    elseif kind == "home" and mission.stage == "drive_home" then
        local target, gate = TAGUP.homeDestination, TAGUP.homeArrival
        if validateReportedVehicleArrival(kind, vehicle, target, gate, reportedX, reportedY, reportedZ) then
            startFinalScene()
        end
    end
end)

addEvent("tagup:vehicleReminder", true)
addEventHandler("tagup:vehicleReminder", resourceRoot, function(stage)
    if source ~= resourceRoot or client ~= mission.leader or not mission.running or stage ~= mission.stage or mission.transitionAudio or
        (stage ~= "drive_idlewood" and stage ~= "drive_ballas" and stage ~= "drive_home") then
        return
    end
    local vehicle = mission.entities.vehicle
    if not isElement(vehicle) or getPedOccupiedVehicle(mission.leader) == vehicle then
        return
    end
    local profile = TAGUP.transitionAudio.reminders[mission.reminderIndex]
    mission.reminderIndex = mission.reminderIndex % #TAGUP.transitionAudio.reminders + 1
    startTransitionAudio(profile, "vehicle_reminder")
end)

addEvent("tagup:storeOffscreenActors", true)
addEventHandler("tagup:storeOffscreenActors", resourceRoot, function(stage, offscreen)
    -- A final rooftop visibility report can already be in flight when the last
    -- tag starts recording 207. Once playback owns the actors, never let that
    -- stale report freeze and store the Greenwood again before playback starts.
    if source ~= resourceRoot or client ~= mission.leader or not mission.running or stage ~= mission.stage or mission.offscreenStored or
        mission.vehiclePlayback or (stage ~= "tags_ballas" and stage ~= "rooftop") then
        return
    end
    local sweet, vehicle = mission.entities.sweet, mission.entities.vehicle
    if not isElement(sweet) or not isElement(vehicle) then
        return
    end
    local px, py, pz = getElementPosition(mission.leader)
    local vx, vy, vz = getElementPosition(vehicle)
    local limits = stage == "tags_ballas" and TAGUP.offscreenStorage.ballasBox or TAGUP.offscreenStorage.rooftopBox
    local outsideBox = math.abs(px - vx) > limits[1] or math.abs(py - vy) > limits[2] or math.abs(pz - vz) > limits[3]
    if (stage == "tags_ballas" and (not outsideBox or offscreen ~= true)) or
        (stage == "rooftop" and not outsideBox and offscreen ~= true) then
        return
    end
    local storage = TAGUP.offscreenStorage.position
    setElementFrozen(vehicle, true)
    setElementFrozen(sweet, true)
    setElementPosition(vehicle, storage[1], storage[2], storage[3])
    mission.offscreenStored = true
    outputDebugString(("[tagging-up-turf] Sweet and Greenwood stored below world at stage=%s outsideBox=%s offscreen=%s"):format(
                          stage, tostring(outsideBox), tostring(offscreen == true)))
end)

local function isNativeTagAlphaStep(previousAlpha, currentAlpha)
    previousAlpha = tonumber(previousAlpha)
    currentAlpha = tonumber(currentAlpha)
    if not previousAlpha or not currentAlpha or previousAlpha ~= math.floor(previousAlpha) or currentAlpha ~= math.floor(currentAlpha) or
        previousAlpha < 0 or previousAlpha > 255 or currentAlpha < 0 or currentAlpha > 255 then
        return false
    end
    return previousAlpha < 255 and currentAlpha == math.min(previousAlpha + 8, 255)
end

-- CShotInfo::Update can process several live spray shots in one frame. A
-- minimum wall-clock interval would reject a legitimate step and make every
-- following previousAlpha disagree with the authoritative byte.
addEvent("tagup:nativeTagProgress", true)
addEventHandler("tagup:nativeTagProgress", resourceRoot, function(targetObject, creator, previousAlpha, currentAlpha)
    local player = client
    if source ~= resourceRoot or not mission.running or not isMissionPlayer(player) or not isElement(targetObject) or
        getElementType(targetObject) ~= "object" or not isElement(creator) or not isNativeTagAlphaStep(previousAlpha, currentAlpha) then
        return
    end
    previousAlpha = tonumber(previousAlpha)
    currentAlpha = tonumber(currentAlpha)

    if targetObject == mission.entities.demoTag then
        local active = mission.demoShoot
        local profile = TAGUP.sweetDemoShoot
        if mission.stage ~= "demo" or player ~= mission.leader or creator ~= mission.entities.sweet or not active or active.ped ~= creator or
            active.nativeCancelled or getElementSyncer(creator) ~= player or getPedWeapon(creator) ~= TAGUP.sprayWeapon then
            return
        end

        local previousProgress = active.progress or 0
        if previousAlpha ~= previousProgress or currentAlpha ~= math.min(previousProgress + 8, 255) then
            return
        end
        local x, y, z = getElementPosition(creator)
        local demo = TAGUP.demoTag
        if tagupDistance3D(x, y, z, demo.x, demo.y, demo.z) > profile.serverMaxDistance then
            return
        end
        active.progress = currentAlpha
        setElementData(targetObject, "tagup.paintAlpha", active.progress, true)
        if math.floor(previousProgress / 64) ~= math.floor(active.progress / 64) then
            outputDebugString(("[tagging-up-turf] Sweet demo tag: %d%% from native spray hits"):format(math.floor(active.progress / 255 * 100)))
        end
        if active.progress == 255 then
            completeDemoTag(active)
        end
        return
    end

    local tagId = tonumber(getElementData(targetObject, "tagup.tagId"))
    if creator ~= player or not tagId or targetObject ~= mission.entities["tag" .. tagId] or mission.completedTags[tagId] or mission.ballasGangScene then
        return
    end

    local active = false
    for _, id in ipairs(activeTagIds()) do
        if id == tagId then
            active = true
            break
        end
    end
    if not active or getPedWeapon(player) ~= TAGUP.sprayWeapon or isPedInVehicle(player) then
        return
    end

    -- The engine has already selected the surface and advanced it by GTA's
    -- exact increment. The server only checks mission authority and mirrors one
    -- native step, never inferring a hit from input or proximity alone.
    local previousProgress = mission.tagProgress[tagId] or 0
    if previousAlpha ~= previousProgress or currentAlpha ~= math.min(previousProgress + 8, 255) then
        return
    end
    local tag = tagupGetTag(tagId)
    local x, y, z = getElementPosition(player)
    if tagupDistance3D(x, y, z, tag.x, tag.y, tag.z) > TAGUP.sprayRange then
        return
    end
    mission.tagProgress[tagId] = currentAlpha
    updateTagVisual(tagId, mission.tagProgress[tagId])
    if math.floor(previousProgress / 64) ~= math.floor(mission.tagProgress[tagId] / 64) then
        outputDebugString(
            ("[tagging-up-turf] Tag %d: %d%% from native hits by %s"):format(tagId, math.floor(mission.tagProgress[tagId] / 255 * 100), getPlayerName(player))
        )
    end
    if mission.tagProgress[tagId] == 255 then
        mission.completedTags[tagId] = true
        replaceTagObject(tagId)
        broadcastState({message = getPlayerName(player) .. " a termine un tag."})
        if tagId == 3 and mission.ballasEncounter and mission.ballasEncounter.phase == "following" and mission.ballasEncounter.followReady then
            startBallasAttack("alley_tag_complete")
        end
        if currentGroupComplete() then
            rememberTimer(setTimer(advanceAfterTags, 900, 1))
        end
    else
        broadcastState()
    end
end)

addEventHandler("onVehicleExplode", root, function()
    if mission.running and source == mission.entities.vehicle then
        failMission("La Greenwood de Sweet a ete detruite.")
    end
end)

addEventHandler("onVehicleExit", root, function(ped)
    local leave = mission.demoLeave
    if mission.running and mission.stage == "demo" and leave and source == leave.vehicle and ped == leave.ped then
        leave.serverExited = true
        outputDebugString(("[tagging-up-turf] Server observed Sweet leave vehicle for native leave-car #%d"):format(leave.id))
        tryCompleteDemoLeave()
    end

    local departure = mission.ballasDeparture
    if mission.running and mission.stage == "ballas_departure" and departure and source == departure.vehicle and isMissionPlayer(ped) then
        outputDebugString(("[tagging-up-turf] Server observed Ballas departure vehicle exit for %s"):format(getPlayerName(ped)))
        if departure.clientExitReports[ped] then
            departure.exitedPlayers[ped] = true
            tryStartBallasWander()
        end
    end
end)

addEvent("tagup:sweetReturnEnterResult", true)
addEventHandler("tagup:sweetReturnEnterResult", resourceRoot, function(enterId, ped, vehicle, result, details)
    local player = client
    local enter = mission.demoEnter
    if source ~= resourceRoot or not mission.running or (mission.stage ~= "tags_idlewood" and mission.stage ~= "return_car") or
        player ~= mission.leader or not isMissionPlayer(player) or not enter or enter.id ~= tonumber(enterId) or enter.ped ~= ped or
        enter.vehicle ~= vehicle or ped ~= mission.entities.sweet or vehicle ~= mission.entities.vehicle or not isElement(ped) or
        not isElement(vehicle) then
        outputDebugString("[tagging-up-turf] Rejected stale or unauthorized Sweet passenger-entry result", 2)
        return
    end

    details = tostring(details or "")
    outputDebugString(("[tagging-up-turf] Sweet native passenger entry #%d result=%s (%s)"):format(
                          enter.id, tostring(result), details:sub(1, 240)))
    if result ~= "entered" then
        cancelDemoEnter("client_" .. tostring(result))
        return failMission("L'entree passager native de Sweet a echoue: " .. tostring(result))
    end
    if getElementSyncer(ped) ~= player then
        cancelDemoEnter("invalid_syncer")
        return failMission("Le resultat d'entree passager de Sweet ne vient plus de son syncer.")
    end

    enter.clientObserved = true
    tryCompleteSweetReturnEnter()
end)

addEventHandler("onVehicleEnter", root, function(ped, seat)
    if mission.running and source == mission.entities.vehicle and ped == mission.entities.sweet and mission.introEntryPending then
        if tonumber(seat) ~= 1 then
            mission.introEntryPending = false
            clearIntroEntryGuard()
            return failMission("Sweet est monte dans le mauvais siege apres la scene d'intro.")
        end
        mission.introEntryPending = false
        clearIntroEntryGuard()
        outputDebugString("[tagging-up-turf] Server observed Sweet complete the intro passenger-entry task in seat 1")
        if mission.stage == "enter_car" and isPartyInVehicle() then
            setStage("drive_idlewood", {message = "Sweet est a bord. Direction Idlewood."})
        end
        return
    end

    local enter = mission.demoEnter
    if not mission.running or not enter or source ~= enter.vehicle or ped ~= enter.ped then
        return
    end

    enter.serverEntered = tonumber(seat) == enter.seat
    outputDebugString(("[tagging-up-turf] Server observed Sweet enter passenger seat %d for native entry #%d"):format(
                          tonumber(seat) or -1, enter.id))
    if not enter.serverEntered then
        cancelDemoEnter("wrong_server_seat")
        return failMission("Sweet est monte dans le mauvais siege.")
    end
    tryCompleteSweetReturnEnter()
end)

addEventHandler("onPedWasted", root, function()
    if not mission.running then
        return
    end
    if source == mission.entities.sweet then
        failMission("Sweet est mort.")
    end
end)

addEventHandler("onElementDestroy", root, function()
    if mission.running and (source == mission.entities.sweet or source == mission.entities.vehicle) and
        (mission.introScene or mission.demoScene or mission.demoLeave or mission.demoWalk or mission.demoShoot or mission.demoEnter or
            mission.ballasDeparture or mission.finalScene) then
        cancelIntroScene("ped_destroyed")
        cancelDemoScene("ped_destroyed")
        cancelDemoLeave("ped_destroyed")
        cancelDemoWalk("ped_destroyed")
        cancelDemoShoot("ped_destroyed")
        cancelDemoEnter("ped_destroyed")
        cancelBallasDeparture("ped_destroyed")
        cancelFinalScene("ped_destroyed")
        failMission("Sweet ou la Greenwood a ete detruit pendant sa demonstration native.")
    elseif mission.running and mission.introScene and source == mission.entities.smoke then
        cancelIntroScene("smoke_destroyed")
        failMission("Big Smoke a ete detruit pendant la scene d'intro.")
    end
end)

addEventHandler("onPlayerWasted", root, function()
    if mission.running and isMissionPlayer(source) then
        if mission.introScene or mission.demoScene or mission.ballasGangScene or mission.finalScene then
            failMission("Un membre de l'equipe est mort pendant une scene de mission.")
            return
        end
        local alive = false
        for _, player in ipairs(mission.party) do
            if isElement(player) and not isPedDead(player) then
                alive = true
                break
            end
        end
        if not alive then
            failMission("Toute l'equipe est morte.")
        end
    end
end)

addEventHandler("onPlayerQuit", root, function()
    if mission.running and isMissionPlayer(source) then
        failMission("Un membre de l'equipe a quitte la mission.")
    end
end)

addEventHandler("onResourceStop", resourceRoot, function()
    cancelFileCutscene("resource_stopped")
    cancelIntroScene("resource_stopped")
    cancelDemoLeave("resource_stopped")
    cancelDemoWalk("resource_stopped")
    cancelDemoShoot("resource_stopped")
    cancelDemoEnter("resource_stopped")
    cancelDemoScene("resource_stopped")
    cancelBallasDeparture("resource_stopped")
    cancelBallasGangScene("resource_stopped")
    cancelFinalScene("resource_stopped")
    clearMissionTimers()
    for _, player in ipairs(mission.party) do
        restorePlayer(player, mission.snapshots[player])
    end
    destroyMissionEntities()
end)

addEventHandler("onResourceStart", resourceRoot, function()
    outputDebugString("[tagging-up-turf] Ready. Use /tagup for the full mission or /tagupfinal for the Grove Street finale (up to three connected players).")
end)
