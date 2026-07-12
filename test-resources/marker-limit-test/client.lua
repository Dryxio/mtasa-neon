local testMarkers = {}
local validTypes = {
    arrow = true,
    checkpoint = true,
    ["checkpoint-arrow"] = true,
    cylinder = true,
    mixed = true,
    ring = true,
}

local function report(message, r, g, b)
    outputChatBox("[marker-limit-test] " .. message, r or 220, g or 220, b or 220)
    outputConsole("[marker-limit-test] " .. message)
end

local function clearMarkers()
    local destroyed = 0
    for _, marker in ipairs(testMarkers) do
        if isElement(marker) then
            destroyElement(marker)
            destroyed = destroyed + 1
        end
    end
    testMarkers = {}
    return destroyed
end

local function formatStats(stats)
    return string.format(
        "streamed=%d/%d, 3D=%d/%d, checkpoints=%d/%d, direction-arrows=%d",
        stats.streamedMarkers,
        stats.streamedMarkerLimit,
        stats.allocated3DMarkers,
        stats.marker3DLimit,
        stats.activeCheckpoints,
        stats.checkpointLimit,
        stats.directionArrowLimit
    )
end

local function reportStats()
    report(formatStats(getMarkerLimitStats()), 120, 220, 255)
end

local function markerTypeForIndex(requestedType, index)
    if requestedType ~= "mixed" then
        return requestedType
    end

    local types = {"cylinder", "arrow", "checkpoint", "checkpoint-arrow", "ring"}
    return types[(index - 1) % #types + 1]
end

local function createTestMarker(markerType, x, y, z, index)
    local nativeType = markerType == "checkpoint-arrow" and "checkpoint" or markerType
    local hue = (index * 47) % 255
    local marker = createMarker(x, y, z, nativeType, 1.5, 64 + hue % 191, 255 - hue, 160 + hue % 95, 180)

    if marker and markerType == "checkpoint-arrow" then
        setMarkerIcon(marker, "arrow")
    end

    return marker
end

local function runTest(_, requestedType, requestedCount)
    requestedType = requestedType and string.lower(requestedType) or "mixed"
    local count = math.floor(tonumber(requestedCount) or 128)

    if not validTypes[requestedType] then
        report("type invalide; utiliser cylinder, arrow, checkpoint, checkpoint-arrow, ring ou mixed", 255, 100, 100)
        return
    end

    if count < 1 or count > 4096 then
        report("count doit etre compris entre 1 et 4096", 255, 100, 100)
        return
    end

    clearMarkers()

    local playerX, playerY, playerZ = getElementPosition(localPlayer)
    local columns = 64
    local spacing = 3.5
    local halfWidth = (columns - 1) * spacing * 0.5
    local created = 0

    for index = 1, count do
        local column = (index - 1) % columns
        local row = math.floor((index - 1) / columns)
        local x = playerX + column * spacing - halfWidth
        local y = playerY + 20 + row * spacing
        local markerType = markerTypeForIndex(requestedType, index)
        local marker = createTestMarker(markerType, x, y, playerZ - 1, index)

        if marker then
            testMarkers[#testMarkers + 1] = marker
            created = created + 1
        end
    end

    report(string.format("%d/%d markers %s crees; mesure apres stabilisation du streamer", created, count, requestedType), 120, 255, 120)
    setTimer(reportStats, 2000, 1)
end

addCommandHandler("markerlimittest", runTest)
addCommandHandler("markerlimitstats", reportStats)
addCommandHandler("markerlimitclear", function()
    report(string.format("%d markers detruits", clearMarkers()), 255, 220, 120)
    setTimer(reportStats, 2000, 1)
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    report("/markerlimittest [type] [count], /markerlimitstats, /markerlimitclear")
    report("frontieres conseillees: 31, 32, 33 puis 128")
end)

addEventHandler("onClientResourceStop", resourceRoot, clearMarkers)
