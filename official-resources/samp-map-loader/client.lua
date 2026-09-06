local activeMaps = {}

local function logDiagnostics(path, diagnostics)
    for _, diagnostic in ipairs(diagnostics or {}) do
        local location = diagnostic.line and diagnostic.line > 0 and ("%s:%d:%d"):format(path, diagnostic.line, diagnostic.column or 0) or path
        outputDebugString(
            ("[SAMP Loader] %s: %s/%s: %s"):format(location, diagnostic.stage or "unknown", diagnostic.severity or "error", diagnostic.message),
            diagnostic.severity == "error" and 1 or 2
        )
    end
end

addCommandHandler("loadsampmap", function(_, path)
    if not path or path == "" then
        outputChatBox("Usage: /loadsampmap :resource/path/to/map.pwn", 255, 180, 80)
        return
    end

    local handle, diagnostics = loadSAMPMap(path)
    logDiagnostics(path, diagnostics)
    if not handle then
        outputChatBox("[SAMP Loader] Load failed; inspect F8/client.log.", 255, 80, 80)
        return
    end
    activeMaps[#activeMaps + 1] = handle
    outputChatBox(
        ("[SAMP Loader] Loaded %d objects using %d SA-MP models."):format(handle.objectCount, handle.remappedModelCount),
        80,
        255,
        160
    )
end)
addCommandHandler("unloadsampmaps", function()
    for index = #activeMaps, 1, -1 do
        unloadSAMPMap(activeMaps[index])
    end
    activeMaps = {}
    outputChatBox("[SAMP Loader] All command-loaded maps unloaded.", 120, 220, 255)
end)

addEventHandler("onClientResourceStop", resourceRoot, function()
    for index = #activeMaps, 1, -1 do
        unloadSAMPMap(activeMaps[index])
    end
end)
