local function assertEqual(actual, expected, message)
    if actual ~= expected then
        error(("%s: expected %s, got %s"):format(message, tostring(expected), tostring(actual)))
    end
end

local function assertNear(actual, expected, tolerance, message)
    if not actual or math.abs(actual - expected) > tolerance then
        error(("%s: expected %.3f, got %s"):format(message, expected, tostring(actual)))
    end
end

function runCityResidencyPolicyTests()
    local cities = {
        alpha = {bounds = {minX = 1000, maxX = 2000, minY = 1000, maxY = 2000}},
        beta = {bounds = {minX = -2000, maxX = -1000, minY = 1000, maxY = 2000}},
    }
    local options = {
        approachPadding = 500,
        criticalPadding = 100,
        predictionPadding = 250,
        predictionHorizon = 15,
        leavePadding = 750,
    }

    local tests = {
        function()
            assertEqual(CityResidencyPolicy.pickCandidate(cities, {x = 0, y = 0}, {x = 0, y = 0}, options), nil, "stationary far")
        end,
        function()
            local candidate = CityResidencyPolicy.pickCandidate(cities, {x = 1200, y = 1200}, {x = 0, y = 0}, options)
            assertEqual(candidate.id, "alpha", "inside city")
            assertEqual(candidate.urgent, true, "inside is urgent")
        end,
        function()
            local candidate = CityResidencyPolicy.pickCandidate(cities, {x = 0, y = 1500}, {x = 100, y = 0}, options)
            assertEqual(candidate.id, "alpha", "fast approach prediction")
            assertNear(candidate.eta, 7.5, 0.001, "prediction entry time")
        end,
        function()
            local candidate = CityResidencyPolicy.pickCandidate(cities, {x = 0, y = 1500}, {x = -100, y = 0}, options)
            assertEqual(candidate.id, "beta", "direction-aware prediction")
        end,
        function()
            local eta = CityResidencyPolicy.segmentEntryTime(0, 1500, 1000, 0, 15, cities.alpha.bounds, 0)
            assertNear(eta, 1, 0.001, "swept segment does not overshoot")
        end,
        function()
            assertEqual(CityResidencyPolicy.contains(750, 1500, cities.alpha.bounds, 250), true, "padded leave boundary")
            assertEqual(CityResidencyPolicy.contains(749, 1500, cities.alpha.bounds, 250), false, "outside leave boundary")
        end,
        function()
            local retained = CityResidencyPolicy.retainCandidate(
                cities,
                "alpha",
                nil,
                {x = 2700, y = 1500},
                {x = 0, y = 0},
                options
            )
            assertEqual(retained.id, "alpha", "leave hysteresis retains candidate")
            assertEqual(retained.latched, true, "retained candidate is marked")
        end,
        function()
            local retained = CityResidencyPolicy.retainCandidate(
                cities,
                "alpha",
                nil,
                {x = 2751, y = 1500},
                {x = 0, y = 0},
                options
            )
            assertEqual(retained, nil, "leave hysteresis eventually clears")
        end,
    }

    local passed = 0
    for index, test in ipairs(tests) do
        local ok, reason = pcall(test)
        if not ok then
            return false, ("test %d failed: %s"):format(index, tostring(reason)), passed, #tests
        end
        passed = passed + 1
    end
    return true, "all policy tests passed", passed, #tests
end
