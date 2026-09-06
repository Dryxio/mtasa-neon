-- Only periodic samples are coalesced. Lifecycle acknowledgements and failures
-- bypass this queue, so batching cannot delay an ownership decision.
VehicleTrafficTransport = {pending = {}, diagnosticsEnabled = false}

function VehicleTrafficTransport.enqueue(unit, evidence, data)
    local key = tostring(unit.id) .. ":" .. tostring(unit.epoch) .. ":" .. evidence
    VehicleTrafficTransport.pending[key] = {unit = unit, epoch = unit.epoch, evidence = evidence, data = data}
end

function VehicleTrafficTransport.flush(isCurrent, send)
    local pending = VehicleTrafficTransport.pending
    VehicleTrafficTransport.pending = {}
    local batch = {}
    for _, sample in pairs(pending) do
        if isCurrent(sample.unit) and sample.unit.epoch == sample.epoch then
            batch[#batch + 1] = {id = sample.unit.id, epoch = sample.epoch, evidence = sample.evidence, data = sample.data}
            if #batch == 32 then
                send(batch)
                batch = {}
            end
        end
    end
    if #batch > 0 then send(batch) end
end
