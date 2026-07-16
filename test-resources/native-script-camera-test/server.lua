local restartTimer

addEvent("nativeScriptCameraTest:restart", true)
addEventHandler("nativeScriptCameraTest:restart", resourceRoot, function()
    if source ~= resourceRoot or not isElement(client) then
        return
    end

    if isTimer(restartTimer) then
        outputChatBox("[native camera] Un restart est deja programme.", client, 255, 180, 80)
        return
    end

    if not hasObjectPermissionTo(getThisResource(), "function.restartResource", false) then
        outputDebugString("[native camera] restart refuse: ACL function.restartResource absente", 2)
        outputChatBox("[native camera] ACL absente. Console serveur: aclrequest allow native-script-camera-test function.restartResource", client, 255, 80, 80)
        return
    end

    outputDebugString(("[native camera] resource restart requested by %s"):format(getPlayerName(client)), 3)
    outputChatBox("[native camera] Restart de la ressource dans 2.5 s; ne lancez aucune autre commande.", client, 255, 180, 80)

    -- The client deliberately does not release its lease when this resource
    -- stops. This restart therefore validates the native resource-cleanup path
    -- instead of masking it with an explicit Lua release.
    restartTimer = setTimer(function()
        restartResource(getThisResource())
    end, 2500, 1)
end)
