-- A successful theft ends ambient AI ownership, not the lifetime of the car.
-- Keep it in the physical traffic budget until safely abandoned or explicitly
-- adopted by another server resource. This module has no remote client events.
TrafficTakeoverLifecycle = {}
local api = TrafficTakeoverLifecycle
local retained = {}
local grace, observerDistance = 60000, 90

local function vehicle(v)
    return isElement(v) and getElementType(v) == "vehicle"
end

local function occupied(v)
    return next(getVehicleOccupants(v) or {}) ~= nil
end

local function attached(v)
    return isElement(getVehicleTowingVehicle(v)) or isElement(getVehicleTowedByVehicle(v))
        or isElement(getElementAttachedTo(v)) or #(getAttachedElements(v) or {}) > 0
end

local function nearby(v, players)
    local x, y, z = getElementPosition(v)
    for _, player in ipairs(players) do
        if isElement(player) and getElementDimension(player) == getElementDimension(v)
            and getElementInterior(player) == getElementInterior(v) then
            local px, py, pz = getElementPosition(player)
            if getDistanceBetweenPoints3D(x, y, z, px, py, pz) <= observerDistance then return true end
        end
    end
    return false
end

function api.retain(v, player)
    if not vehicle(v) then return false end
    -- Duplicate callbacks cannot reset abandonment or revoke an accepted claim.
    if retained[v] then return true end
    retained[v] = {player = player, pending = true}
    return true
end

function api.confirm(v)
    local r = retained[v]
    if not r or not vehicle(v) or getVehicleOccupant(v, 0) ~= r.player then return false end
    r.pending = false
    r.confirmed = true
    r.emptySince = nil
    return true
end

function api.expire(v)
    local r = retained[v]
    if not r then return false end
    -- Entry timeout and quit only release the pending transaction. Cleanup
    -- still has to respect a late occupant, a tow link and nearby observers.
    r.pending = false
    return true
end

function api.count()
    local count = 0
    for v in pairs(retained) do
        if vehicle(v) then count = count + 1 else retained[v] = nil end
    end
    return count
end

function api.remaining(cap)
    return math.max(0, cap - api.count())
end

function api.update()
    if not next(retained) then return end
    local now, players = getTickCount(), getElementsByType("player")
    for v, r in pairs(retained) do
        if not vehicle(v) then retained[v] = nil
        elseif occupied(v) or attached(v) then r.emptySince = nil
        elseif not r.pending then
            r.emptySince = r.emptySince or now
            if (now - r.emptySince) % 4294967296 >= grace and not nearby(v, players)
                and retained[v] == r and not occupied(v) and not attached(v) then
                -- A failed engine call must not release its budget slot while
                -- the physical element still exists.
                if destroyElement(v) then retained[v] = nil end
            end
        end
    end
end

-- Explicit ownership contract for trusted server resources. Success only
-- releases this transient cleanup policy. The caller must persist or migrate
-- its car before the creating traffic resource is restarted.
function adoptTrafficVehicle(v)
    local r = retained[v]
    if client or not sourceResource or sourceResource == getThisResource()
        or getResourceState(sourceResource) ~= "running" or not vehicle(v)
        or not r or not r.confirmed or r.pending then return false end
    retained[v] = nil
    return true
end

addEventHandler("onElementDestroy", root, function() retained[source] = nil end)
setTimer(api.update, 1000, 0)
