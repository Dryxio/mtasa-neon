MirrorFloorTest = {
    dimension = 4243,
    -- Keep the custom test out of the exterior world. A floor mirror reflects
    -- everything below its plane, so interior 0 made the San Andreas map appear
    -- overhead when the platform was suspended above it.
    interior = 1,
    panelModel = 3095,
    custom = {
        x = 0,
        y = 2500,
        floorZ = 200,
        width = 29.8,
        depth = 29.8,
        minZ = 200,
        maxZ = 210,
        -- Lift the test plane above the platform so the reflection boundary is
        -- obvious instead of being hidden inside the floor geometry.
        mirrorV = 202.0,
        normalX = 0,
        normalY = 0,
        normalZ = 1,
    },
    vanillaBarber = {
        x = 412.5,
        y = -21.0,
        z = 1001.8,
        rotation = 0,
        interior = 2,
        dimension = 0,
    },
}
