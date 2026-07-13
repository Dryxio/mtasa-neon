local tileDefinitions = {
    { path = "assets/tile_37_18.txd", cells = { { 37, 18 }, { 1, 18 } } },
    { path = "assets/tile_38_18.txd", cells = { { 38, 18 }, { 2, 18 } } },
    { path = "assets/tile_39_18.txd", cells = { { 39, 18 }, { 3, 18 } } },
    { path = "assets/tile_37_19.txd", cells = { { 37, 19 }, { 1, 19 } } },
    { path = "assets/tile_38_19.txd", cells = { { 38, 19 }, { 2, 19 } } },
    { path = "assets/tile_39_19.txd", cells = { { 39, 19 }, { 3, 19 } } },
    { path = "assets/tile_37_20.txd", cells = { { 37, 20 }, { 1, 20 } } },
    { path = "assets/tile_38_20.txd", cells = { { 38, 20 }, { 2, 20 } } },
    { path = "assets/tile_39_20.txd", cells = { { 39, 20 }, { 3, 20 } } },
}

local loadedTiles = {}

local function outputStats(prefix)
    local stats = engineGetRadarMapStats()
    local message = ("%s hooks=%s registered=%d loaded=%d failed=%d source=%.1f KiB"):format(
        prefix,
        tostring(stats.hooksInstalled),
        stats.registeredTiles,
        stats.loadedTiles,
        stats.failedTiles,
        stats.sourceBytes / 1024
    )
    outputChatBox(message, 80, 220, 255)
    outputDebugString(message)
end

local function unloadTiles()
    for _, definition in ipairs(tileDefinitions) do
        for _, cell in ipairs(definition.cells) do
            engineResetRadarMapTile(cell[1], cell[2])
        end
    end
    for _, txd in pairs(loadedTiles) do
        if isElement(txd) then
            destroyElement(txd)
        end
    end
    loadedTiles = {}
end

local function loadTiles()
    unloadTiles()

    for _, definition in ipairs(tileDefinitions) do
        local txd = engineLoadTXD(definition.path)
        if not txd then
            outputChatBox("[Radar test] Echec engineLoadTXD: " .. definition.path, 255, 80, 80)
            return false
        end
        for _, cell in ipairs(definition.cells) do
            if not engineSetRadarMapTile(cell[1], cell[2], txd) then
                outputChatBox(("[Radar test] Echec tuile %d,%d"):format(cell[1], cell[2]), 255, 80, 80)
                destroyElement(txd)
                return false
            end
        end
        loadedTiles[definition.path] = txd
    end

    -- The native San Andreas block is intentionally protected (logical 14..25).
    local vanillaRejected = not engineSetRadarMapTile(14, 14, loadedTiles["assets/tile_38_19.txd"])
    outputChatBox("[Radar test] Bloc vanilla protege: " .. (vanillaRejected and "OK" or "ECHEC"), vanillaRejected and 80 or 255, vanillaRejected and 255 or 80, 120)
    outputStats("[Radar test] charge")
    return true
end

addCommandHandler("radarstats", function()
    outputStats("[Radar test]")
end)

addCommandHandler("radarreload", loadTiles)

addCommandHandler("radarmissing", function()
    local key = "assets/tile_38_19.txd"
    local txd = loadedTiles[key]
    if isElement(txd) then
        -- Destroying the TXD must unregister both cells that reference it.
        destroyElement(txd)
        loadedTiles[key] = nil
    else
        engineResetRadarMapTile(38, 19)
        engineResetRadarMapTile(2, 19)
    end
    outputStats("[Radar test] centre supprime")
end)

addEventHandler("onClientResourceStart", resourceRoot, loadTiles)
addEventHandler("onClientResourceStop", resourceRoot, unloadTiles)
