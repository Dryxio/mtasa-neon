local SHOWCASE_DIMENSION = 64992

-- The video is meant to demonstrate Neon fracturing objects that GTA:SA does not
-- already treat as breakable. Filter native breakables at runtime so a future
-- runway edit cannot accidentally reintroduce one.
addEventHandler("onClientElementCreate", root, function()
    local element = source
    if getElementType(element) ~= "object" then return end

    -- createObject fires this event before client.lua has assigned the showcase
    -- dimension, so defer the check until the current Lua turn has completed.
    setTimer(function()
        if not isElement(element) then return end
        if getElementDimension(element) ~= SHOWCASE_DIMENSION then return end

        local model = getElementModel(element)
        if model and isObjectBreakable(model) then
            destroyElement(element)
        end
    end, 0, 1)
end)
