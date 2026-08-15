NATIVE_AI_HARNESS = {
    schema = "neon.native_ai.harness",
    schemaVersion = 1,
    dimension = 219,
    preparationTimeout = 10000,
    scenarioTimeout = 20000,
    rotationTurnInterval = 450,
    rotationFinalObservation = 1200,
    rotationObservationMinimumMs = 500,
    rotationOwnerHoldMinimumMs = 120,
    rotationConvergenceDegrees = 2.0,
    rotationConvergenceFrames = 3,
    traceLeadTime = 1500,
    cleanupDelay = 2500,
    meleeDamageRadius = 5.0,
    scenarios = {
        melee = {
            id = "remote-melee-group-v1",
            fightingStyle = 4,
            pedModels = {102, 103},
            owner = {2490.0, -1668.0, 13.35, 90.0},
            victim = {2496.0, -1668.0, 13.35, 270.0},
            peds = {
                {2494.0, -1667.2, 13.35, 90.0},
                {2494.0, -1668.8, 13.35, 90.0},
            },
        },
        handoff = {
            id = "native-group-handoff-v1",
            fightingStyle = 4,
            pedModels = {105, 106},
            owner = {2490.0, -1668.0, 13.35, 90.0},
            victim = {2496.0, -1668.0, 13.35, 270.0},
            peds = {
                {2493.5, -1667.2, 13.35, 90.0},
                {2493.5, -1668.8, 13.35, 90.0},
            },
        },
        rotation = {
            id = "isolated-ped-rotation-v2",
            fightingStyle = 4,
            pedModels = {105, 106},
            owner = {2490.0, -1668.0, 13.35, 90.0},
            victim = {2496.0, -1668.0, 13.35, 270.0},
            peds = {
                {2493.5, -1667.2, 13.35, 0.0},
                {2493.5, -1668.8, 13.35, 0.0},
            },
            -- Start away from the actor's initial zero heading. Each change is
            -- exactly 90 degrees, so every action must produce a spatial
            -- sample and a no-op first action cannot hide a transport defect.
            rotationTargets = {45.0, 135.0, 225.0},
        },
        rotation_handoff = {
            id = "isolated-ped-rotation-handoff-v2",
            fightingStyle = 4,
            pedModels = {105, 106},
            owner = {2490.0, -1668.0, 13.35, 90.0},
            victim = {2496.0, -1668.0, 13.35, 270.0},
            peds = {
                {2493.5, -1667.2, 13.35, 0.0},
                {2493.5, -1668.8, 13.35, 0.0},
            },
            rotationTargets = {45.0, 135.0, 225.0},
        },
    },
}

NATIVE_AI_HARNESS_TRACE_KEYS = {
    "neon:nativeAIRunId",
    "neon:nativeAIScenarioId",
    "neon:nativeAIActorId",
    "neon:nativeAIActionId",
    "neon:nativeAIStep",
}
