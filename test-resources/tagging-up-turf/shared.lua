TAGUP = {
    dimension = 4101,
    maximumPlayers = 3,
    sprayWeapon = 41,
    sprayRange = 6,
    tagModel = 1490,
    vehicleModel = 492,
    sweetModel = 270,
    start = {2508.16, -1666.47, 13.0, 16},
    sweetStart = {2518.07, -1668.82, 13.1, 90},
    idlewoodDestination = {2089.01, -1649.08, 12.54},
    ballasDestination = {2338.74, -1500.31, 22.83},
    homeDestination = {2504.74, -1672.45, 12.38},
    -- GTA's IPL loader applies the conjugated quaternion heading. Preserve
    -- that convention here: MTA yaw = (360 - raw IPL quaternion yaw) % 360.
    demoTag = {x = 2102.195313, y = -1648.757813, z = 13.585938, rotation = 0.3},
    sweetDemoWalk = {
        start = {x = 2095.80, y = -1649.86, z = 12.70, heading = 277},
        target = {x = 2100.48, y = -1649.14, z = 12.47},
        movement = "walk",
        radius = 0.5,
        slowdownRadius = 2.0,
        timeout = 20000,
        serverCompletionRadius = 1.25,
        guardTimeout = 27000,
    },
    sweetDemoShoot = {
        duration = 15000,
        burstLength = 5,
        shootingRate = 100,
        weaponAccuracy = 90,
        progressInterval = 100,
        progressPerTick = 0.05,
        postCompletionWait = 1000,
        guardTimeout = 23000,
        serverMaxDistance = 6,
    },
    tags = {
        {id = 1, group = "idlewood", x = 2066.429688, y = -1652.476563, z = 14.28125, rotation = 179.5},
        {id = 2, group = "idlewood", x = 2046.40625, y = -1635.84375, z = 13.585938, rotation = 359.5},
        {id = 3, group = "ballas", x = 2394.101563, y = -1468.367188, z = 24.78125, rotation = 89.5},
        {id = 4, group = "ballas", x = 2353.539063, y = -1508.210938, z = 24.75, rotation = 359.5},
        {id = 5, group = "rooftop", x = 2399.414063, y = -1552.03125, z = 28.75, rotation = 269.5},
    },
    stages = {
        intro = {title = "TAGGING UP TURF", objective = "Grove Street. Sweet a un boulot pour vous."},
        enter_car = {title = "La Greenwood de Sweet", objective = "Montez tous dans la voiture de Sweet."},
        drive_idlewood = {title = "Idlewood", objective = "Conduisez Sweet jusqu'a Idlewood."},
        demo = {title = "Regardez Sweet", objective = "Sweet vous montre comment recouvrir un tag."},
        tags_idlewood = {title = "Deux tags", objective = "Recouvrez les deux tags de Ballas avec la bombe."},
        return_car = {title = "On bouge", objective = "Retournez a la Greenwood de Sweet."},
        drive_ballas = {title = "Territoire Ballas", objective = "Conduisez jusque dans le territoire Ballas."},
        tags_ballas = {title = "Couvrez-vous", objective = "Recouvrez les deux tags. Les Ballas vont reagir."},
        rooftop = {title = "Dernier tag", objective = "Montez sur le toit et recouvrez le dernier tag."},
        return_after_roof = {title = "Retrouvez Sweet", objective = "Retournez a la Greenwood."},
        drive_home = {title = "Grove Street", objective = "Ramenez Sweet chez lui."},
        complete = {title = "MISSION PASSED", objective = "Respect +"},
        failed = {title = "MISSION FAILED", objective = "La mission a echoue."},
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
