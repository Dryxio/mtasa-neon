TAGUP = {
    dimension = 4101,
    maximumPlayers = 3,
    sprayWeapon = 41,
    sprayRange = 6,
    tagModel = 1490,
    vehicleModel = 492,
    sweetModel = 270,
    missionActorData = "tagup.missionActor",
    start = {2508.16, -1666.47, 13.0, 16},
    sweetStart = {2518.07, -1668.82, 13.1, 90},
    idlewoodDestination = {2089.01, -1649.08, 12.54},
    idlewoodArrival = {radiusX = 4.0, radiusY = 4.0, radiusZ = 4.0},
    ballasDestination = {2338.74, -1500.31, 22.83},
    ballasArrival = {radiusX = 4.0, radiusY = 4.0, radiusZ = 4.0},
    homeDestination = {2504.74, -1672.45, 12.38},
    homeArrival = {radiusX = 4.0, radiusY = 4.0, radiusZ = 4.0},
    -- GTA's IPL loader applies the conjugated quaternion heading. Preserve
    -- that convention here: MTA yaw = (360 - raw IPL quaternion yaw) % 360.
    demoTag = {x = 2102.195313, y = -1648.757813, z = 13.585938, rotation = 0.3},
    sweetDemoWalk = {
        target = {x = 2100.48, y = -1649.14, z = 12.47},
        movement = "walk",
        radius = 0.5,
        slowdownRadius = 2.0,
        timeout = 20000,
        serverCompletionRadius = 1.25,
        guardTimeout = 27000,
    },
    sweetDemoLeave = {
        observationTimeout = 5000,
        guardTimeout = 15000,
    },
    sweetDemoShoot = {
        duration = 15000,
        burstLength = 5,
        shootingRate = 100,
        weaponAccuracy = 90,
        postCompletionWait = 1000,
        guardTimeout = 23000,
        serverMaxDistance = 6,
    },
    sweetDemoScene = {
        audio = {approach = 37416, checkout = 37444, engineRunning = 37445},
        camera = {
            establishing = {
                position = {x = 2082.5332, y = -1650.2444, z = 14.8007},
                target = {x = 2083.5154, y = -1650.2024, z = 14.6181},
            },
            staged = {
                position = {x = 2095.5315, y = -1659.4850, z = 13.3156},
                target = {x = 2095.7422, y = -1658.5193, z = 13.4672},
            },
            dialogue = {
                position = {x = 2102.1096, y = -1647.6721, z = 13.1493},
                target = {x = 2101.3691, y = -1648.3153, z = 13.3438},
            },
            sprayMove = {
                from = {x = 2097.2688, y = -1648.2150, z = 14.4973},
                to = {x = 2098.3191, y = -1650.5167, z = 14.3662},
                duration = 10000,
            },
            sprayTrack = {
                from = {x = 2101.6265, y = -1649.5768, z = 13.7631},
                to = {x = 2101.8901, y = -1647.9247, z = 13.7316},
                duration = 10000,
            },
        },
        leaderStage = {x = 2094.70, y = -1652.05, z = 12.65, heading = 302},
        partyOffsets = {{x = -1.0, y = -0.8}, {x = -1.3, y = 0.7}},
        sweetStage = {x = 2095.80, y = -1649.86, z = 12.70, heading = 277},
        sweetReturn = {
            target = {x = 2099.10, y = -1650.02, z = 12.57},
            movement = "walk",
            radius = 0.5,
            slowdownRadius = 2.0,
            timeout = 12000,
            serverCompletionRadius = 1.25,
            guardTimeout = 18000,
        },
        leaderFinal = {x = 2097.56, y = -1651.36, z = 12.71, heading = 99},
        sweetFinal = {x = 2099.10, y = -1650.02, z = 12.57, heading = 99},
        sweetLeaveLead = 600,
        playerExitObservationTimeout = 5000,
        fadeOutDelay = 500,
        fadeOutDuration = 1.0,
        blackStageDelay = 1100,
        blackHold = 500,
        fadeInDuration = 0.5,
        skipArmDelay = 2000,
        -- A first request can stream SCRIPT from disk instead of the warm GTA
        -- cache seen on retries. The vehicle is anchored during this barrier.
        readyTimeout = 30000,
        audioTimeout = 30000,
        animationTimeout = 10000,
        finalCheckTimeout = 2500,
    },
    sweetReturnEnter = {
        seat = 1,
        observationTimeout = 5000,
        scmTimeout = 15000,
        guardTimeout = 20000,
    },
    ballasDeparture = {
        speed = 20.0,
        drivingStyle = "avoid_cars",
        audio = {
            event = 37420,
            loadTimeout = 30000,
            finishTimeout = 20000,
        },
        -- SWEET1 keeps this exact jump-cut shot active from before CJ exits
        -- through SWE1_AV, then until one second after Sweet starts DriveWander.
        camera = {
            position = {x = 2329.6750, y = -1499.9113, z = 25.8505},
            target = {x = 2330.6533, y = -1499.9260, z = 25.6440},
            minimumLeadTime = 100,
            readyTimeout = 5000,
            finalCheckTimeout = 2000,
        },
        observationTimeout = 5000,
        exitTimeout = 15000,
        -- A cold audio load begins before the camera, then overlaps leave-car.
        -- Its natural finish still precedes 05D2 observation and WAIT 1000.
        guardTimeout = 65000,
        postStartWait = 1000,
    },
    ballasGangScene = {
        -- SWEET1's LOCATE_CHAR_ANY_MEANS_2D uses independent X/Y half-axes,
        -- not a radial distance. The leader fills CJ's role in co-op.
        trigger = {x = 2395.61, y = -1470.52, spawnRadiusX = 50.0, spawnRadiusY = 50.0, radiusX = 20.0, radiusY = 17.0},
        camera = {
            position = {x = 2400.3840, y = -1472.2081, z = 23.9349},
            target = {x = 2399.4900, y = -1471.7634, z = 23.9880},
        },
        preSkipWait = 500,
        skippableDuration = 6500,
        readyTimeout = 5000,
        finalCheckTimeout = 2000,
        approach = {x = 2395.61, y = -1470.52, radiusX = 5.0, radiusY = 5.0, retryInterval = 250},
        follow = {
            timeout = -1,
            radius = 0.5,
            angles = {90.0, 270.0},
            attackDelay = 5000,
        },
        audio = {
            whatTheFuck = 37423,
            getThatFool = 37427,
        },
    },
    vehicleRecording207 = {
        id = 207,
        loadTimeout = 10000,
        ownershipTimeout = 5000,
        guardTimeout = 20000,
        nominalElapsed = 7719,
        minimumElapsed = 6500,
        maximumElapsed = 12000,
        endPosition = {2381.0720, -1528.4404, 23.6556},
        serverEndRadius = 8,
    },
    postRoofScene = {
        startDelay = 2000,
        hornLeadDelay = 3500,
        guardTimeout = 60000,
        audio = {
            dialogueEvent = 37430,
            hornEvent = 1147,
            loadTimeout = 30000,
            finishTimeout = 20000,
        },
        preload = {x = 2385.4443, y = -1529.3350, z = 24.0351, heading = 82.7482},
        camera = {
            position = {x = 2386.4436, y = -1529.3130, z = 24.0696},
            target = {x = 2385.4443, y = -1529.3350, z = 24.0351},
            fadeDuration = 0.3,
            readyTimeout = 10000,
            releaseTimeout = 3000,
        },
    },
    tags = {
        {id = 1, group = "idlewood", x = 2066.429688, y = -1652.476563, z = 14.28125, rotation = 179.5},
        {id = 2, group = "idlewood", x = 2046.40625, y = -1635.84375, z = 13.585938, rotation = 359.5},
        {id = 3, group = "ballas", x = 2394.101563, y = -1468.367188, z = 24.78125, rotation = 89.5},
        {id = 4, group = "ballas", x = 2353.539063, y = -1508.210938, z = 24.75, rotation = 359.5},
        {id = 5, group = "rooftop", x = 2399.414063, y = -1552.03125, z = 28.75, rotation = 269.5},
    },
}

function tagupDistance3D(ax, ay, az, bx, by, bz)
    local dx, dy, dz = ax - bx, ay - by, az - bz
    return math.sqrt(dx * dx + dy * dy + dz * dz)
end

function tagupGetTag(id)
    for _, tag in ipairs(TAGUP.tags) do
        if tag.id == id then
            return tag
        end
    end
    return nil
end
