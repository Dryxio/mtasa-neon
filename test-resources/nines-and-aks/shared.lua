NINES = {
    dimension = 4103,
    gxt = "SWEET2",
    command = "nines",
    cutscenes = {intro = "SWEET3A", emmet = "SWEET3B"},
    cutsceneVisibleAreas = {intro = 1},
    cj = {
        position = {2515.28, -1673.44, 13.73, 81.20},
        clothes = {
            {"vest", "vest", 0},
            {"player_face", "head", 1},
            {"jeansdenim", "jeans", 2},
            {"sneakerbincblk", "sneaker", 3},
        },
    },
    glendale = {
        model = 466,
        -- Installed Glendale COL3 base offset: 0.5036026.
        position = {2506.23, -1675.75, 12.8736026, 148.0},
        plate = "_A2TMFK_",
        primary = {77, 93, 96},
        secondary = {214, 218, 214},
    },
    smoke = {model = 269, position = {2505.8691, -1672.7229, 13.3778, 160.0}, health = 500},
    -- Neon reserves playable model 302 for the EMMET special-model mapping.
    emmet = {model = 302, position = {2451.8340, -1976.8108, 13.5469, 75.6292}, health = 600},
    tampa = {
        model = 549,
        -- Installed Tampa COL3 base offset: 0.5041911.
        position = {2446.49, -1965.0, 13.0441911, 101.85},
        plate = "_FELTCH_",
    },
    destinations = {
        emmet = {2453.07, -2003.96, 12.56, 4.0},
        smoke = {2066.4648, -1695.4436, 12.5547, 4.0},
        binco = {2246.9719, -1660.7789, 14.2856, 4.0},
    },
    -- CREATE_OBJECT adds DYN_WINE_BIG's centre offset; native shoot tasks keep the raw SCM Z.
    bottleCenterOffset = 0.240264,
    bottleRounds = {
        [1] = {
            demo = {{2440.58, -1979.89, 14.340264}},
            player = {{2440.58, -1979.89, 14.440264}},
        },
        [2] = {
            demo = {{2440.58, -1979.89, 14.340264}, {2440.14, -1976.50, 13.640264}, {2440.95, -1973.76, 14.440264}},
            player = {{2440.58, -1979.89, 14.340264}, {2440.14, -1976.50, 13.640264}, {2440.95, -1973.76, 14.440264}},
        },
        [3] = {
            demo = {{2440.58, -1979.89, 14.340264}, {2440.14, -1976.50, 13.640264}, {2440.66, -1973.76, 14.340264},
                    {2448.97, -1973.29, 13.540264}, {2444.35, -1970.61, 13.940264}},
            player = {{2440.58, -1979.89, 14.340264}, {2440.14, -1976.50, 13.640264}, {2440.66, -1973.76, 14.340264},
                      {2448.97, -1973.29, 13.540264}, {2444.35, -1970.61, 13.940264}},
        },
    },
    bottleModel = 1551,
    range = {center = {2448.9602, -1973.5446, 13.3074}, radius = {13.0, 13.0, 10.0}},
    binco = {
        outside = {2246.9719, -1660.7789, 14.2856, 0.0},
        insideSpawn = {207.0, -109.0, 1005.1328, 15, 0.0},
        clothes = {208.0279, -107.9499, 1005.1328},
        exit = {207.5, -112.0, 1005.1328},
        outsideExit = {2244.48, -1664.0599, 14.469, 180.0},
    },
    audio = {
        driveOut = {
            {37800, "SWE3_AA", "cj"}, {37801, "SWE3_AB", "smoke"}, {37802, "SWE3_AC", "smoke"},
            {37803, "SWE3_AD", "smoke"}, {37804, "SWE3_AE", "smoke"}, {37805, "SWE3_AF", "cj"},
            {37806, "SWE3_AH", "smoke"}, {37807, "SWE3_AJ", "smoke"}, {37808, "SWE3_AK", "cj"},
        },
        range = {
            smoke1 = {37811, "SWE3_BC", "smoke"}, praise1 = {37831, "SWE3_DC", "emmet"},
            smoke2 = {37809, "SWE3_BA", "smoke"}, praise2 = {37829, "SWE3_DA", "emmet"},
            smoke3 = {{37812, "SWE3_BD", "smoke"}, {37813, "SWE3_BE", "smoke"}, {37814, "SWE3_BF", "smoke"},
                      {37815, "SWE3_BG", "smoke"}, {37810, "SWE3_BB", "smoke"}, {37873, "SWE3_ZZ", "cj"}},
            praise3 = {37835, "SWE3_DG", "emmet"},
        },
        emmetLeave = {
            {37850, "SWE3_GA", "smoke"}, {37851, "SWE3_GB", "smoke"}, {37852, "SWE3_GC", "emmet"},
            {37853, "SWE3_GD", "smoke"}, {37854, "SWE3_GE", "smoke"}, {37855, "SWE3_GF", "smoke"},
            {37856, "SWE3_GG", "emmet"}, {37857, "SWE3_GH", "emmet"}, {37858, "SWE3_GJ", "emmet"},
            {37859, "SWE3_GK", "emmet"}, {37860, "SWE3_GL", "emmet"}, {37861, "SWE3_GM", "smoke"},
            {37862, "SWE3_GN", "cj"}, {37863, "SWE3_GO", "smoke"},
        },
        driveBack = {
            {37864, "SWE3_HA", "cj"}, {37865, "SWE3_HB", "smoke"}, {37866, "SWE3_HC", "cj"},
            {37867, "SWE3_HD", "smoke"}, {37868, "SWE3_HE", "smoke"}, {37869, "SWE3_HF", "cj"},
            {37870, "SWE3_HG", "smoke"},
        },
        goodbye = {{37871, "SWE3_JA", "smoke"}, {37872, "SWE3_JB", "cj"}},
        phone = {
            {29066, "MSWE07A", "cj"}, {29067, "MSWE07B", "smoke"}, {29068, "MSWE07C", "cj"},
            {29069, "MSWE07D", "smoke"}, {29070, "MSWE07E", "cj"}, {29071, "MSWE07F", "smoke"},
            {29072, "MSWE07G", "smoke"}, {29073, "MSWE07H", "smoke"}, {29074, "MSWE07J", "cj"},
            {29075, "MSWE07K", "smoke"},
        },
        reminders = {
            {35800, "SMOX_AA", "smoke"}, {35801, "SMOX_AB", "smoke"}, {35802, "SMOX_AC", "smoke"},
            {35803, "SMOX_AD", "smoke"}, {35804, "SMOX_AE", "smoke"},
        },
    },
}
