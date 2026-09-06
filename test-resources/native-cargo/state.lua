-- Pure server reservation protocol. Native completion is a presentation report,
-- never proof of mission delivery; reward/arrival checks belong on the server.
CargoState = {}
function CargoState.new()
    return { phase = 'available', revision = 0 }
end
function CargoState.reserve(row, ped, executor, now, allowed)
    if row.phase ~= 'available' or not ped or not executor or not allowed then return false end
    row.revision = row.revision + 1
    row.phase, row.ped, row.executor, row.deadline = 'reserved', ped, executor, now + 8000
    return true
end
function CargoState.report(row, sender, revision, state, now)
    if sender ~= row.executor or revision ~= row.revision or row.phase == 'available' then return false end
    if state ~= 'released' and CargoState.expired(row, now) then return false end
    if state == 'holding' and row.phase == 'reserved' then
        row.phase, row.deadline = 'carrying', nil
    elseif state == 'putting_down' and row.phase == 'carrying' then
        row.phase, row.deadline = 'putting_down', now + 8000
    elseif state == 'released' then
        return CargoState.release(row)
    else
        return false
    end
    return true
end
function CargoState.release(row)
    if row.phase == 'available' then return false end
    row.revision = row.revision + 1 -- Invalidates in-flight reports from the previous lease.
    row.phase, row.ped, row.executor, row.deadline = 'available', nil, nil, nil
    return true
end
function CargoState.expired(row, now)
    return row.deadline ~= nil and now >= row.deadline
end
