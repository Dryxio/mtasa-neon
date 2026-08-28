local function submitObservation()
    if not isElement(localPlayer) then
        return
    end
    -- The client only signals that this exact bundled resource reached its
    -- start event. Version and identity fields are derived server-side from
    -- the remote player element and are never trusted from event payloads.
    triggerServerEvent("neonAgentProof:client", resourceRoot)
end

addEventHandler("onClientResourceStart", resourceRoot, submitObservation)
