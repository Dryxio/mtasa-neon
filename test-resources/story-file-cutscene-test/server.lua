local run

local function clearRun(reason)
    local active = run
    run = nil
    if not active then return end
    if isElement(active.cutscene) then
        exports["story-world-runtime"]:releaseStoryFileCutscene(active.cutscene)
    end
    if isElement(active.modelLease) then
        exports["story-world-runtime"]:releaseStoryPlayerModelLease(active.modelLease)
    end
    outputServerLog(("[story file cutscene test] cleanup reason=%s"):format(tostring(reason)))
end

local function finishRun(active, verdict, details)
    if run ~= active or active.finishing then return end
    active.finishing = true
    active.verdict, active.details, active.cleanupStartedAt = verdict, details, getTickCount()
    local finalize
    finalize = function()
        if run ~= active then return end
        local cutsceneState = isElement(active.cutscene) and
                                  exports["story-world-runtime"]:getStoryFileCutsceneState(active.cutscene)
        if cutsceneState ~= "released" and cutsceneState ~= "failed" then
            if not active.cleanupRequested then
                active.cleanupRequested = true
                exports["story-world-runtime"]:releaseStoryFileCutscene(active.cutscene)
            end
            if getTickCount() - active.cleanupStartedAt < 12000 then return setTimer(finalize, 50, 1) end
            active.verdict = "FAIL"
            active.details = "terminal cutscene cleanup timed out"
            if isElement(active.cutscene) then
                destroyElement(active.cutscene)
                active.cutsceneForced = true
            end
        end
        local cutsceneReleased = active.cutsceneForced or
                                     (isElement(active.cutscene) and
                                         exports["story-world-runtime"]:releaseStoryFileCutscene(active.cutscene))
        local modelLeaseReleased = isElement(active.modelLease) and
                                       exports["story-world-runtime"]:releaseStoryPlayerModelLease(active.modelLease)
        local restored, restorationReason = true, ""
        for _, entry in ipairs(active.originalModels) do
            if isElement(entry.player) and getElementModel(entry.player) ~= entry.model then
                restored = false
                restorationReason = ("model restore failed for %s: expected %d got %d"):format(
                                        getPlayerName(entry.player), entry.model, getElementModel(entry.player))
                break
            end
        end
        if cutsceneReleased ~= true or isElement(active.cutscene) or modelLeaseReleased ~= true or
            isElement(active.modelLease) or not restored then
            active.verdict = "FAIL"
            active.details = ("cleanup cutscene=%s/%s modelLease=%s/%s restored=%s %s"):format(
                                 tostring(cutsceneReleased), tostring(not isElement(active.cutscene)),
                                 tostring(modelLeaseReleased), tostring(not isElement(active.modelLease)),
                                 tostring(restored), restorationReason)
        end
        run = nil
        outputServerLog(("[story file cutscene test] %s name=%s participants=%d states=%s details=%s"):format(
                            active.verdict, active.name, #active.players, table.concat(active.states, ">"),
                            tostring(active.details or "")))
    end
    setTimer(finalize, 50, 1)
end

addEventHandler("onStoryFileCutsceneStateChange", root, function(state, snapshot)
    local active = run
    if not active or source ~= active.cutscene then return end
    active.states[#active.states + 1] = state
    outputServerLog(("[story file cutscene test] state=%s loaded=%d/%d started=%d finished=%d released=%d"):format(
                        state, snapshot.loaded, snapshot.participants, snapshot.started, snapshot.finished,
                        snapshot.released))
    if state == "failed" then return finishRun(active, "FAIL", snapshot.reason) end
    local participants = snapshot.participants
    if (state == "loaded" and snapshot.loaded ~= participants) or
        (state == "started" and snapshot.started ~= participants) or
        (state == "finished" and snapshot.finished ~= participants) or
        (state == "released" and snapshot.released ~= participants) then
        return finishRun(active, "FAIL", "state was published before its all-client barrier")
    end
    if state ~= "released" then return end
    local expected = {"loaded", "started", "finished", "released"}
    for index, expectedState in ipairs(expected) do
        if active.states[index] ~= expectedState then
            return finishRun(active, "FAIL", ("state %d expected %s, got %s"):format(
                                 index, expectedState, tostring(active.states[index])))
        end
    end
    finishRun(active, "PASS", snapshot.skipped and "skipped" or "natural")
end)

addCommandHandler("storycutscenetest", function(player, _, name, visibleArea)
    if run then return outputServerLog("[story file cutscene test] FAIL another run is active") end
    local players = getElementsByType("player")
    local leader = isElement(player) and getElementType(player) == "player" and player or players[1]
    name = type(name) == "string" and name ~= "" and name or "SWEET1A"
    visibleArea = visibleArea and tonumber(visibleArea) or nil
    if #players == 0 or not leader then
        return outputServerLog("[story file cutscene test] FAIL no connected participant")
    end
    local originalModels = {}
    for _, participant in ipairs(players) do
        originalModels[#originalModels + 1] = {player = participant, model = getElementModel(participant)}
    end
    local modelLease, leaseReason = exports["story-world-runtime"]:createStoryPlayerModelLease(players, 0)
    if not modelLease then
        return outputServerLog("[story file cutscene test] FAIL model lease: " .. tostring(leaseReason))
    end
    local cutscene, reason = exports["story-world-runtime"]:createStoryFileCutscene(players, leader, name,
                                                                                   visibleArea, {
        allowLeaderSkip = true,
        loadTimeout = 15000,
        playTimeout = 180000,
        releaseTimeout = 10000,
    })
    if not cutscene then
        exports["story-world-runtime"]:releaseStoryPlayerModelLease(modelLease)
        return outputServerLog("[story file cutscene test] FAIL create: " .. tostring(reason))
    end
    run = {cutscene = cutscene, modelLease = modelLease, players = players, leader = leader, name = name,
           states = {}, originalModels = originalModels}
    outputServerLog(("[story file cutscene test] START name=%s participants=%d leader=%s"):format(
                        name, #players, getPlayerName(leader)))
end)

addCommandHandler("storycutscenetestskip", function(player)
    if not run then return outputServerLog("[story file cutscene test] FAIL no active run") end
    if isElement(player) and getElementType(player) == "player" and player ~= run.leader then
        return outputServerLog("[story file cutscene test] FAIL only the leader may request the test skip")
    end
    local skipped = exports["story-world-runtime"]:skipStoryFileCutscene(run.cutscene)
    outputServerLog("[story file cutscene test] server skip accepted=" .. tostring(skipped))
end)

addEventHandler("onResourceStop", resourceRoot, function() clearRun("resource_stopped") end)
