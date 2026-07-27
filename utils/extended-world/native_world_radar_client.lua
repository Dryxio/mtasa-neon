local GRID_SIZE = 40
local STOCK_MIN = 14
local STOCK_MAX = 25

local catalog = NATIVE_WORLD_RADAR_CATALOG
local loadedTiles = {}
local registeredTiles = {}
local state = {
    status = "idle",
    error = false,
    registeredTiles = 0,
}

local function setFailure(reason)
    state.status = "failed"
    state.error = reason
    outputDebugString(("[NativeWorldRadar] pack=%s state=failed reason=%s"):format(
        catalog and tostring(catalog.packId) or "unknown",
        tostring(reason)
    ), 1)
end

local function isInteger(value)
    return type(value) == "number" and value == math.floor(value)
end

local function isStockCell(column, row)
    return column >= STOCK_MIN and column <= STOCK_MAX and row >= STOCK_MIN and row <= STOCK_MAX
end

local function validateCatalog()
    if type(catalog) ~= "table" then
        return false, "catalog is missing"
    end
    if type(catalog.packId) ~= "string" or catalog.packId == "" then
        return false, "packId is invalid"
    end
    if type(catalog.displayName) ~= "string" or catalog.displayName == "" then
        return false, "displayName is invalid"
    end
    if type(catalog.tiles) ~= "table" or #catalog.tiles == 0 then
        return false, "tile catalog is empty"
    end

    local cells = {}
    local paths = {}
    for index, tile in ipairs(catalog.tiles) do
        if type(tile) ~= "table" or type(tile.path) ~= "string" or tile.path == "" then
            return false, ("tile %d has an invalid path"):format(index)
        end
        if not isInteger(tile.column) or not isInteger(tile.row) or tile.column < 0 or tile.column >= GRID_SIZE or
            tile.row < 0 or tile.row >= GRID_SIZE then
            return false, ("tile %d is outside the 40x40 radar grid"):format(index)
        end
        if isStockCell(tile.column, tile.row) then
            return false, ("tile %d overlaps protected San Andreas cell %d,%d"):format(index, tile.column, tile.row)
        end

        local cell = ("%d,%d"):format(tile.column, tile.row)
        if cells[cell] then
            return false, ("duplicate radar cell %s"):format(cell)
        end
        if paths[tile.path] then
            return false, ("duplicate radar path %s"):format(tile.path)
        end
        cells[cell] = true
        paths[tile.path] = true
    end
    return true
end

local function releaseRadar()
    if engineResetRadarMapTile then
        for _, tile in ipairs(registeredTiles) do
            engineResetRadarMapTile(tile.column, tile.row)
        end
    end
    registeredTiles = {}

    for _, txd in ipairs(loadedTiles) do
        if isElement(txd) then
            destroyElement(txd)
        end
    end
    loadedTiles = {}
    state.status = "idle"
    state.error = false
    state.registeredTiles = 0
end

local function loadRadar()
    releaseRadar()

    local valid, reason = validateCatalog()
    if not valid then
        setFailure(reason)
        return false
    end
    if not engineLoadTXD or not engineSetRadarMapTile or not engineResetRadarMapTile or not engineGetRadarMapStats then
        setFailure("this client does not expose the extended-radar API")
        return false
    end

    local radarStats = engineGetRadarMapStats()
    if type(radarStats) ~= "table" or radarStats.hooksInstalled ~= true then
        setFailure("native radar hooks are unavailable for this executable")
        return false
    end

    -- Load every source first so a corrupt or missing TXD cannot leave a
    -- partially registered city in the global radar catalog.
    for index, tile in ipairs(catalog.tiles) do
        local txd = engineLoadTXD(tile.path)
        if not txd then
            releaseRadar()
            setFailure(("failed to load tile %d (%s)"):format(index, tile.path))
            return false
        end
        loadedTiles[#loadedTiles + 1] = txd
    end

    for index, tile in ipairs(catalog.tiles) do
        if not engineSetRadarMapTile(tile.column, tile.row, loadedTiles[index]) then
            releaseRadar()
            setFailure(("cell %d,%d is invalid or owned by another resource"):format(tile.column, tile.row))
            return false
        end
        registeredTiles[#registeredTiles + 1] = tile
    end

    state.status = "active"
    state.error = false
    state.registeredTiles = #registeredTiles
    radarStats = engineGetRadarMapStats()
    outputDebugString(("[NativeWorldRadar] pack=%s state=active tiles=%d global=%d sourceBytes=%d"):format(
        catalog.packId,
        #registeredTiles,
        tonumber(radarStats.registeredTiles) or -1,
        tonumber(radarStats.sourceBytes) or -1
    ))
    return true
end

function getNativeWorldRadarStatus()
    local radarStats = engineGetRadarMapStats and engineGetRadarMapStats() or {}
    return {
        packId = catalog and catalog.packId or false,
        displayName = catalog and catalog.displayName or false,
        status = state.status,
        error = state.error,
        registeredTiles = state.registeredTiles,
        hooksInstalled = radarStats.hooksInstalled == true,
        globalRegisteredTiles = tonumber(radarStats.registeredTiles) or 0,
        loadedTiles = tonumber(radarStats.loadedTiles) or 0,
        failedTiles = tonumber(radarStats.failedTiles) or 0,
        sourceBytes = tonumber(radarStats.sourceBytes) or 0,
    }
end

function reloadNativeWorldRadar()
    return loadRadar()
end

local function printStats()
    local stats = getNativeWorldRadarStatus()
    outputChatBox(("%s radar: %s, own %d, global %d, decoded %d, failed %d, %.1f MiB compressed"):format(
        stats.displayName or "Native world",
        stats.status,
        stats.registeredTiles,
        stats.globalRegisteredTiles,
        stats.loadedTiles,
        stats.failedTiles,
        stats.sourceBytes / (1024 * 1024)
    ), 200, 230, 255)
end

addEventHandler("onClientResourceStart", resourceRoot, loadRadar)
addEventHandler("onClientResourceStop", resourceRoot, releaseRadar)

if catalog and type(catalog.statsCommand) == "string" and catalog.statsCommand ~= "" then
    addCommandHandler(catalog.statsCommand, printStats)
end
