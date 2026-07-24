TAGUP = {
    dimension = 4101,
    maximumPlayers = 3,
    sprayWeapon = 41,
    sprayRange = 6,
    tagModel = 1490,
    vehicleModel = 492,
    vehiclePlate = "GROVE4L",
    bounceRadioChannel = 4,
    vehicleDoorLock = {unlocked = 1, playerOnly = 3},
    sweetModel = 270,
    cj = {
        model = 0,
        clothingSlots = 18,
        -- A fresh vanilla game installs these four clothes before storing
        -- CJ's initial appearance. SWEET1 never replaces that appearance.
        clothes = {
            {texture = "vest", model = "vest", type = 0},
            {texture = "player_face", model = "head", type = 1},
            {texture = "jeansdenim", model = "jeans", type = 2},
            {texture = "sneakerbincblk", model = "sneaker", type = 3},
        },
    },
    -- GTA's character placement opcodes convert their script-space Z from
    -- the model base to the ped centre. The verified CJ and mission-ped
    -- models in SWEET1 all use this 1.0 m conversion. Movement task targets
    -- remain raw script coordinates and must not use this offset.
    scmCharacterPlacementZOffset = 1.0,
    missionActorData = "tagup.missionActor",
    -- CREATE_CAR receives the model base position; MTA creates vehicles from
    -- their centre, which is 0.5 m higher for the Greenwood.
    start = {2508.16, -1666.47, 13.5, 16},
    vehicleColors = {78, 104, 129, 100, 100, 100},
    sweetStart = {2518.07, -1668.82, 13.1, 90},
    fileCutscene = {
        name = "SWEET1A",
        fadeInDuration = 1.0,
        appearanceTimeout = 10000,
        appearanceStableSamples = 3,
        loadTimeout = 60000,
        finishTimeout = 50000,
        releaseTimeout = 5000,
        pollInterval = 50,
    },
    introScene = {
        -- This is the world-space scene after the separate SWEET1A cutscene.
        -- The coordinates and ordering come directly from the installed SCM.
        leaderStart = {x = 2516.53, y = -1671.37, z = 12.88, heading = 61.28},
        leaderWalk = {x = 2512.64, y = -1671.07, z = 12.50},
        leaderFinalWalk = {x = 2510.2705, y = -1669.8031, z = 12.4092},
        sweetStart = {x = 2518.07, y = -1668.82, z = 12.85, heading = 103.76},
        sweetWalkingStyle = 122,
        sweetWalk = {x = 2513.8118, y = -1669.5271, z = 12.5348},
        sweetFinal = {x = 2510.05, y = -1666.61, z = 12.57, heading = 53.89},
        preload = {x = 2515.42, y = -1671.19, z = 14.46},
        smoke = {
            model = 269,
            walkingStyle = 124,
            start = {x = 2520.10, y = -1669.58, z = 13.10, heading = 173.99},
            walk = {x = 2518.5093, y = -1676.1243, z = 13.3855},
        },
        camera = {
            fixed = {
                position = {x = 2512.0100, y = -1671.2073, z = 14.2722},
                target = {x = 2512.9377, y = -1670.8423, z = 14.1948},
            },
            move = {
                from = {x = 2523.4170, y = -1664.9910, z = 15.1254},
                to = {x = 2522.9614, y = -1667.4600, z = 15.2254},
                duration = 13000,
            },
            track = {
                from = {x = 2513.6946, y = -1670.6090, z = 13.8556},
                to = {x = 2514.2966, y = -1669.3425, z = 13.9372},
                duration = 13000,
            },
            nearClip = 0.2,
            fadeInDuration = 0.5,
        },
        audio = {
            {event = 37400, key = "SWE1_AA", speaker = "sweet"},
            {event = 37401, key = "SWE1_AB", speaker = "leader"},
            {event = 37402, key = "SWE1_AC", speaker = "sweet"},
            {event = 37403, key = "SWE1_AD", speaker = "sweet"},
            {event = 37404, key = "SWE1_AE", speaker = "leader"},
        },
        audioGap = 200,
        postAudioWait = 1000,
        readyTimeout = 30000,
        audioLoadTimeout = 30000,
        audioFinishTimeout = 15000,
        releaseTimeout = 3000,
        entryRequestTimeout = 7000,
        entryTimeout = 20000,
    },
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
        sequenceGuardTimeout = 45000,
        serverMaxDistance = 6,
    },
    sweetDemoScene = {
        audio = {approach = 37416, checkout = 37444, engineRunning = 37445},
        facialTalkDuration = 3000,
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
    transitionAudio = {
        engineRunning = {event = 37445, key = "SWE1_CB", duration = 2500, followUp = "SWE1_X", followUpDuration = 6000},
        ballasDeparture = {event = 37417, key = "SWE1_AS", duration = 3000, blocking = true},
        groveReturn = {event = 38607, key = "SWEX_AH", duration = 3000, followUp = "SWE1_B", followUpDuration = 6000},
        reminders = {
            {event = 37434, key = "SWE1_BM", duration = 3000},
            {event = 38644, key = "SWEX_BS", duration = 3000},
            {event = 38641, key = "SWEX_BP", duration = 3000},
            {event = 37429, key = "SWE1_BG", duration = 3000},
        },
        loadTimeout = 30000,
        finishTimeout = 20000,
        releaseTimeout = 3000,
    },
    offscreenStorage = {
        position = {2487.1721, -1666.3010, -100.3438},
        -- These are opcode LOCATE_CHAR_ANY_MEANS_CAR_3D axis extents, not
        -- radial distances. SWEET1 intentionally uses a different boolean at
        -- the rooftop: outside this box OR off screen.
        ballasBox = {30.0, 30.0, 10.0},
        rooftopBox = {100.0, 100.0, 10.0},
        reportInterval = 250,
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
    finalScene = {
        -- Installed main.scm offsets 0x7BF25 through 0x7C51C stage the
        -- complete Grove Street conversation before mission_sweet1_passed.
        leader = {x = 2511.3518, y = -1672.14, z = 12.4588, heading = 180.0},
        sweet = {x = 2511.3518, y = -1673.14, z = 12.4588, heading = 0.0},
        extraPlayers = {
            {x = 2497.0, y = -1682.0, z = 13.0, heading = 0.0},
            {x = 2495.5, y = -1682.0, z = 13.0, heading = 0.0},
        },
        camera = {
            fixed = {
                position = {x = 2509.9500, y = -1672.0947, z = 13.7315},
                target = {x = 2510.8914, y = -1672.4194, z = 13.8204},
            },
            move = {
                from = {x = 2510.1528, y = -1673.5514, z = 14.2267},
                to = {x = 2509.9971, y = -1671.8660, z = 13.9831},
                duration = 18000,
            },
            track = {
                from = {x = 2510.9673, y = -1672.9948, z = 14.0633},
                to = {x = 2510.8853, y = -1672.3169, z = 13.8960},
                duration = 18000,
            },
            nearClip = 0.2,
            fadeOutDuration = 1.0,
            fadeInDuration = 1.0,
            skipFadeDuration = 0.25,
        },
        audio = {
            {event = 37435, key = "SWE1_BN", speaker = "sweet"},
            {event = 37436, key = "SWE1_BO", speaker = "leader"},
            {event = 37437, key = "SWE1_BP", speaker = "sweet"},
            {event = 37438, key = "SWE1_BQ", speaker = "leader"},
            {event = 37443, key = "SWE1_BX", speaker = "leader"},
            {event = 37441, key = "SWE1_BT", speaker = "sweet"},
            {event = 37442, key = "SWE1_BU", speaker = "sweet"},
        },
        -- start_talking_sweet1 applies the generic GTA FACTALK request for
        -- exactly two seconds, then stop_talking_sweet1 clears it when the
        -- mission-audio sample finishes.
        facialTalkDuration = 2000,
        audioStartDelay = 700,
        leaderIdleChatLine = 4,
        handshakeLine = 6,
        walkLine = 7,
        handshake = {block = "GANGS", name = "hndshkfa", guardTimeout = 10000},
        sweetWalk = {
            target = {x = 2517.3972, y = -1677.3524, z = 13.2548},
            movement = "walk",
            radius = 0.5,
            slowdownRadius = 2.0,
            timeout = 20000,
        },
        postAudioWait = 1500,
        readyTimeout = 30000,
        visualReadyTimeout = 5000,
        visualStableSamples = 5,
        actorPositionTolerance = 0.2,
        actorHeadingTolerance = 5.0,
        audioLoadTimeout = 30000,
        audioFinishTimeout = 15000,
        taskReportTimeout = 5000,
        releaseTimeout = 3000,
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

function tagupScmCharacterPlacementZ(scriptZ)
    return scriptZ + TAGUP.scmCharacterPlacementZOffset
end

function tagupGetTag(id)
    for _, tag in ipairs(TAGUP.tags) do
        if tag.id == id then
            return tag
        end
    end
    return nil
end
