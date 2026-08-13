local TEST_X, TEST_Y, TEST_Z = 2731.6, -2493.0, 14.0

local function placePlayerForTest(player)
    if not isElement(player) then
        return
    end

    if isPedDead(player) then
        spawnPlayer(player, TEST_X, TEST_Y, TEST_Z, 0, 0, 0, 0)
    else
        setElementPosition(player, TEST_X, TEST_Y, TEST_Z)
        setElementRotation(player, 0, 0, 0)
        setElementInterior(player, 0)
        setElementDimension(player, 0)
    end

    fadeCamera(player, true)
    setCameraTarget(player, player)
    outputChatBox("[SAMP MVP] Test WOSA charge a quelques metres devant toi.", player, 80, 255, 160)
end

addEventHandler("onPlayerJoin", root, function()
    setTimer(placePlayerForTest, 1000, 1, source)
end)

addEventHandler("onResourceStart", resourceRoot, function()
    for _, player in ipairs(getElementsByType("player")) do
        placePlayerForTest(player)
    end
end)

addCommandHandler("wosatest", function(player)
    placePlayerForTest(player)
end)
