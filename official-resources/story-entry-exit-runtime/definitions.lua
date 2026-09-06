STORY_ENTRY_EXIT_DEFINITIONS = {
    cschp_ls = {
        name = "CSCHP",
        site = "cschp_ls",
        outside = {
            interior = 0,
            trigger = {
                x = 2244.47,
                y = -1665.36,
                z = 15.4839,
                rotation = 0.0,
                radiusX = 0.8,
                radiusY = 0.8,
                zTolerance = 1.0,
            },
            destination = {x = 2244.48, y = -1664.06, z = 15.4839, rotation = 357.0},
        },
        inside = {
            interior = 15,
            trigger = {
                x = 207.738,
                y = -111.42,
                z = 1005.27,
                rotation = 0.0,
                radiusX = 0.8,
                radiusY = 0.7,
                zTolerance = 1.0,
            },
            -- CFileLoader::LoadEntryExit adds 1.0 to every IPL entry and exit Z
            -- before CEntryExitManager uses the linked pair.
            destination = {x = 207.738, y = -109.02, z = 1005.27, rotation = 0.0},
        },
    },
}
