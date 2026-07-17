local session

addEvent("nativeGangTag:start", true)
addEventHandler("nativeGangTag:start", resourceRoot, function(sessionId, object)
    if session and isElement(session.object) then
        releaseObjectGangTag(session.object)
    end
    session = {id = sessionId, object = object}
    if type(acquireObjectGangTag) ~= "function" or not acquireObjectGangTag(object, 0) then
        outputDebugString("[native-gang-tag-test] acquireObjectGangTag refused", 1)
    end
end)

addEventHandler("onClientObjectGangTagProgress", root, function(previousAlpha, currentAlpha, creator)
    if not session or source ~= session.object or creator ~= localPlayer then
        return
    end
    triggerServerEvent("nativeGangTag:progress", resourceRoot, session.id, source, previousAlpha, currentAlpha)
end)

addEvent("nativeGangTag:sync", true)
addEventHandler("nativeGangTag:sync", resourceRoot, function(sessionId, object, alpha)
    if session and session.id == sessionId and session.object == object and isElement(object) then
        local predictedAlpha = getObjectGangTagProgress(object)
        -- Native hits can run ahead of their server acknowledgements. Never
        -- let an older acknowledgement roll local prediction backward, since
        -- that would make the next genuine +8 event appear discontinuous.
        if type(predictedAlpha) ~= "number" or alpha > predictedAlpha then
            setObjectGangTagProgress(object, alpha)
        end
    end
end)

addEvent("nativeGangTag:stop", true)
addEventHandler("nativeGangTag:stop", resourceRoot, function(sessionId, object)
    if session and session.id == sessionId and session.object == object then
        if isElement(object) then
            releaseObjectGangTag(object)
        end
        session = nil
    end
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    if session and isElement(session.object) and type(releaseObjectGangTag) == "function" then
        releaseObjectGangTag(session.object)
    end
    session = nil
end)
