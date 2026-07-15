CityResidencyPolicy = {}

local function expandedBounds(bounds, padding)
    padding = padding or 0
    return {
        minX = bounds.minX - padding,
        maxX = bounds.maxX + padding,
        minY = bounds.minY - padding,
        maxY = bounds.maxY + padding,
    }
end

function CityResidencyPolicy.distanceToBounds(x, y, bounds, padding)
    local area = expandedBounds(bounds, padding)
    local dx = math.max(area.minX - x, 0, x - area.maxX)
    local dy = math.max(area.minY - y, 0, y - area.maxY)
    return math.sqrt(dx * dx + dy * dy)
end

function CityResidencyPolicy.contains(x, y, bounds, padding)
    return CityResidencyPolicy.distanceToBounds(x, y, bounds, padding) == 0
end

function CityResidencyPolicy.segmentEntryTime(x, y, velocityX, velocityY, horizon, bounds, padding)
    local area = expandedBounds(bounds, padding)
    local entry, leave = 0, horizon

    local function clip(position, velocity, minimum, maximum)
        if math.abs(velocity) < 0.0001 then
            return position >= minimum and position <= maximum
        end

        local first = (minimum - position) / velocity
        local second = (maximum - position) / velocity
        if first > second then
            first, second = second, first
        end
        entry = math.max(entry, first)
        leave = math.min(leave, second)
        return entry <= leave
    end

    if not clip(x, velocityX, area.minX, area.maxX) or not clip(y, velocityY, area.minY, area.maxY) then
        return nil
    end
    if leave < 0 or entry > horizon then
        return nil
    end
    return math.max(0, entry)
end

function CityResidencyPolicy.pickCandidate(cities, position, velocity, options)
    local candidates = {}
    for id, city in pairs(cities) do
        local distance = CityResidencyPolicy.distanceToBounds(position.x, position.y, city.bounds)
        local eta = CityResidencyPolicy.segmentEntryTime(
            position.x,
            position.y,
            velocity.x,
            velocity.y,
            options.predictionHorizon,
            city.bounds,
            options.predictionPadding
        )
        local approaching = distance <= options.approachPadding or eta ~= nil
        if approaching then
            candidates[#candidates + 1] = {
                id = id,
                distance = distance,
                eta = eta,
                inside = distance == 0,
                urgent = distance <= options.criticalPadding,
                predicted = distance > options.approachPadding and eta ~= nil,
            }
        end
    end

    table.sort(candidates, function(left, right)
        if left.urgent ~= right.urgent then
            return left.urgent
        end
        if left.inside ~= right.inside then
            return left.inside
        end
        local leftEta = left.eta or math.huge
        local rightEta = right.eta or math.huge
        if leftEta ~= rightEta then
            return leftEta < rightEta
        end
        if left.distance ~= right.distance then
            return left.distance < right.distance
        end
        return left.id < right.id
    end)

    return candidates[1]
end

function CityResidencyPolicy.retainCandidate(cities, previousId, candidate, position, velocity, options)
    if candidate or not previousId or not cities[previousId] then
        return candidate
    end

    local city = cities[previousId]
    local distance = CityResidencyPolicy.distanceToBounds(position.x, position.y, city.bounds)
    local eta = CityResidencyPolicy.segmentEntryTime(
        position.x,
        position.y,
        velocity.x,
        velocity.y,
        options.predictionHorizon,
        city.bounds,
        options.predictionPadding
    )
    if distance > options.leavePadding and eta == nil then
        return nil
    end

    return {
        id = previousId,
        distance = distance,
        eta = eta,
        inside = distance == 0,
        urgent = distance <= options.criticalPadding,
        predicted = eta ~= nil,
        latched = true,
    }
end
