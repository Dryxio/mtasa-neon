local tyrePolicyKey = "nativeTaskRuntime.vehicleTyresCanBurst"

function setSynchronizedVehicleTyresCanBurst(vehicle, canBurst)
    if not isElement(vehicle) or getElementType(vehicle) ~= "vehicle" then
        return false, "vehicule invalide"
    end
    if type(canBurst) ~= "boolean" then
        return false, "politique de pneus invalide"
    end

    -- A string preserves the distinction between an explicit false policy and
    -- missing element data while the vehicle streams independently per client.
    return setElementData(vehicle, tyrePolicyKey, canBurst and "allow" or "prevent")
end
