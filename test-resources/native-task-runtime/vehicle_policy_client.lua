local tyrePolicyKey = "nativeTaskRuntime.vehicleTyresCanBurst"

local function applyTyrePolicy(vehicle)
    if getElementType(vehicle) ~= "vehicle" or type(setVehicleTyresCanBurst) ~= "function" then
        return
    end
    local policy = getElementData(vehicle, tyrePolicyKey)
    if policy == "allow" then
        setVehicleTyresCanBurst(vehicle, true)
    elseif policy == "prevent" then
        setVehicleTyresCanBurst(vehicle, false)
    end
end

addEventHandler("onClientElementStreamIn", root, function()
    applyTyrePolicy(source)
end)

addEventHandler("onClientElementDataChange", root, function(key)
    if key == tyrePolicyKey then
        applyTyrePolicy(source)
    end
end)

addEventHandler("onClientResourceStart", resourceRoot, function()
    for _, vehicle in ipairs(getElementsByType("vehicle", root, true)) do
        applyTyrePolicy(vehicle)
    end
end)
