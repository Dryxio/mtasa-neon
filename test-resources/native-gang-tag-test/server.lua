local DIMENSION = 4192
local sessions = {}
local serial = 0

local function stopSession(player, restore)
    local session = sessions[player]
    if not session then
        return
    end
    sessions[player] = nil
    triggerClientEvent(player, "nativeGangTag:stop", resourceRoot, session.id, session.object)
    if isElement(session.object) then
        destroyElement(session.object)
    end
    if restore and isElement(player) then
        takeWeapon(player, 41)
        if session.sprayAmmo > 0 then
            giveWeapon(player, 41, session.sprayAmmo, false)
        end
        setElementInterior(player, session.interior)
        setElementDimension(player, session.dimension)
        setElementPosition(player, session.x, session.y, session.z)
        setElementRotation(player, 0, 0, session.rotation)
        if session.weapon ~= 41 then
            setPedWeaponSlot(player, getSlotFromWeapon(session.weapon))
        end
    end
end

addCommandHandler("nativegangtag", function(player)
    stopSession(player, true)

    local x, y, z = getElementPosition(player)
    local _, _, rotation = getElementRotation(player)
    local spraySlot = getSlotFromWeapon(41)
    serial = serial + 1
    local object = createObject(1490, 2102.195313, -1648.757813, 13.585938, 0, 0, 0.3)
    if not object then
        outputChatBox("Native gang-tag test: object creation failed.", player, 255, 80, 80)
        return
    end

    setElementDimension(object, DIMENSION)
    setElementCollisionsEnabled(object, false)
    sessions[player] = {
        id = serial,
        object = object,
        alpha = 0,
        x = x,
        y = y,
        z = z,
        rotation = rotation,
        dimension = getElementDimension(player),
        interior = getElementInterior(player),
        weapon = getPedWeapon(player),
        sprayAmmo = getPedTotalAmmo(player, spraySlot),
    }

    setElementInterior(player, 0)
    setElementDimension(player, DIMENSION)
    setElementPosition(player, 2100.48, -1649.14, 12.47)
    setElementRotation(player, 0, 0, 277)
    giveWeapon(player, 41, 500, true)
    triggerClientEvent(player, "nativeGangTag:start", resourceRoot, serial, object)
    outputDebugString(("[native-gang-tag-test] session=%d started player=%s object=%s"):format(serial, getPlayerName(player), tostring(object)))
    outputChatBox("Spray the nearby tag. GTA must advance it in exact 8-alpha steps to PASS.", player, 120, 220, 255)
end)

addEvent("nativeGangTag:progress", true)
addEventHandler("nativeGangTag:progress", resourceRoot, function(sessionId, object, previousAlpha, currentAlpha)
    local player = client
    local session = sessions[player]
    previousAlpha = tonumber(previousAlpha)
    currentAlpha = tonumber(currentAlpha)
    local valid = source == resourceRoot and session and session.id == tonumber(sessionId) and session.object == object and isElement(object) and
                      previousAlpha and currentAlpha and currentAlpha == math.min(previousAlpha + 8, 255) and previousAlpha == session.alpha
    if not valid then
        outputDebugString(("[native-gang-tag-test] FAIL session=%s expectedSession=%s previous=%s current=%s authoritative=%s objectMatch=%s element=%s"):format(
                              tostring(sessionId), tostring(session and session.id), tostring(previousAlpha), tostring(currentAlpha),
                              tostring(session and session.alpha), tostring(session and session.object == object), tostring(isElement(object))), 1)
        outputChatBox("Native gang-tag test FAIL: invalid or discontinuous native alpha step.", player, 255, 80, 80)
        stopSession(player, true)
        return
    end

    session.alpha = currentAlpha
    triggerClientEvent(player, "nativeGangTag:sync", resourceRoot, session.id, object, session.alpha)
    if session.alpha == 255 then
        outputDebugString(("[native-gang-tag-test] PASS session=%d player=%s alpha=255"):format(session.id, getPlayerName(player)))
        outputChatBox("Native gang-tag test PASS: GTA reached alpha 255 through native spray hits.", player, 80, 255, 120)
    end
end)

addCommandHandler("nativegangtagcleanup", function(player)
    stopSession(player, true)
end)

addEventHandler("onPlayerQuit", root, function()
    stopSession(source, false)
end)

addEventHandler("onResourceStop", resourceRoot, function()
    for player in pairs(sessions) do
        stopSession(player, true)
    end
end)
