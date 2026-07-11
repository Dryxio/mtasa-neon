local coronas = {}
local defaultCoronaCount = 120

local function destroyTestCoronas()
    for _, marker in ipairs(coronas) do
        if isElement(marker) then
            destroyElement(marker)
        end
    end

    coronas = {}
end

local function createTestCoronas(_, requestedCount)
    destroyTestCoronas()

    local coronaCount = math.min(math.max(tonumber(requestedCount) or defaultCoronaCount, 1), 4094)

    local x, y, z = getElementPosition(localPlayer)
    local columns = math.ceil(math.sqrt(coronaCount))
    local spacing = 2.5

    for index = 0, coronaCount - 1 do
        local column = index % columns
        local row = math.floor(index / columns)
        local red = math.floor(255 * column / math.max(columns - 1, 1))
        local green = math.floor(255 * row / math.max(math.ceil(coronaCount / columns) - 1, 1))
        local blue = 255 - red
        local marker = createMarker(
            x + (column - 5.5) * spacing,
            y + 8 + row * spacing,
            z + 1.2,
            "corona",
            1.25,
            red,
            green,
            blue,
            220
        )

        if marker then
            table.insert(coronas, marker)
        end
    end

    outputChatBox(
        ("[Corona test] %d/%d coronas creees. /coronatest [1-4094] pour changer le test."):format(#coronas, coronaCount),
        80,
        255,
        160
    )
end

addEventHandler("onClientResourceStart", resourceRoot, function()
    setTimer(function()
        createTestCoronas(nil, defaultCoronaCount)
    end, 1500, 1)
end)

addCommandHandler("coronatest", createTestCoronas)

addEventHandler("onClientResourceStop", resourceRoot, destroyTestCoronas)
