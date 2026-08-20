RADIO_SELECTOR_STATIONS = {
    [1] = {
        name = "Playback FM",
        short = "PLAYBACK",
        genre = "CLASSIC HIP HOP",
        icon = "icons/radio_playback.png",
        primary = "#8b2d24",
        secondary = "#d68a35",
        accent = "#fff0b3",
        mark = "PLAY\nBACK",
    },
    [2] = {
        name = "K-Rose",
        short = "K-ROSE",
        genre = "CLASSIC COUNTRY",
        icon = "icons/radio_krose.png",
        primary = "#d7b976",
        secondary = "#8c342d",
        accent = "#fff5d6",
        mark = "K\nROSE",
    },
    [3] = {
        name = "K-DST",
        short = "K-DST",
        genre = "CLASSIC ROCK",
        icon = "icons/radio_KDST.png",
        primary = "#27231f",
        secondary = "#b79a45",
        accent = "#f4e0a1",
        mark = "K-DST",
    },
    [4] = {
        name = "Bounce FM",
        short = "BOUNCE",
        genre = "FUNK / DISCO / SOUL",
        icon = "icons/radio_bounce.png",
        primary = "#5c2d78",
        secondary = "#e279a6",
        accent = "#fff2a8",
        mark = "BOUNCE",
    },
    [5] = {
        name = "SF-UR",
        short = "SF-UR",
        genre = "UNDERGROUND HOUSE",
        icon = "icons/radio_SFUR.png",
        primary = "#204f59",
        secondary = "#65b8b4",
        accent = "#e7ffff",
        mark = "SF-UR",
    },
    [6] = {
        name = "Radio Los Santos",
        short = "RADIO LS",
        genre = "WEST COAST HIP HOP",
        icon = "icons/radio_RLS.png",
        primary = "#2f4934",
        secondary = "#bc3f31",
        accent = "#f0d66b",
        mark = "LOS\nSANTOS",
    },
    [7] = {
        name = "Radio X",
        short = "RADIO X",
        genre = "ALTERNATIVE ROCK",
        icon = "icons/radio_RADIOX.png",
        primary = "#1e2023",
        secondary = "#a63732",
        accent = "#f1eee9",
        mark = "X",
    },
    [8] = {
        name = "CSR 103.9",
        short = "CSR",
        genre = "NEW JACK SWING / R&B",
        icon = "icons/radio_csr.png",
        primary = "#d5b7bd",
        secondary = "#7c455d",
        accent = "#fff9f2",
        mark = "CSR\n103.9",
    },
    [9] = {
        name = "K-Jah West",
        short = "K-JAH",
        genre = "REGGAE / DUB / DANCEHALL",
        icon = "icons/radio_kjah.png",
        primary = "#315a35",
        secondary = "#d3a53c",
        accent = "#f4e3a0",
        mark = "K-JAH\nWEST",
    },
    [10] = {
        name = "Master Sounds 98.3",
        short = "MASTER",
        genre = "RARE GROOVE / SOUL",
        icon = "icons/radio_mastersounds.png",
        primary = "#33261d",
        secondary = "#c18a42",
        accent = "#f5e6bd",
        mark = "MASTER\n98.3",
    },
    [11] = {
        name = "WCTR",
        short = "WCTR",
        genre = "WEST COAST TALK RADIO",
        icon = "icons/radio_WCTR.png",
        primary = "#34546d",
        secondary = "#9bb5c8",
        accent = "#f2f7fa",
        mark = "WCTR",
    },
    [12] = {
        name = "User Track Player",
        short = "USER TRACKS",
        genre = "ON DEMAND",
        icon = "icons/radio_TPLAYER.png",
        primary = "#3f444b",
        secondary = "#aeb4bd",
        accent = "#f6f8fb",
        mark = "USER\nTRACKS",
    },
}

local function track(title, artist)
    return {title = title, artist = artist}
end

-- GTA reports a zero-based music-track index. The PC audio archives use their
-- own order, which differs from the published station playlists, so this
-- catalogue follows the native archive order used by the radio manager.
RADIO_SELECTOR_TRACKS = {
    [1] = {
        [0] = track("Me and the Biz", "Masta Ace"),
        [1] = track("Warm It Up, Kane", "Big Daddy Kane"),
        [2] = track("Road to the Riches", "Kool G Rap & DJ Polo"),
        [3] = track("Rebel Without a Pause", "Public Enemy"),
        [4] = track("It Takes Two", "Rob Base and DJ E-Z Rock"),
        [5] = track("I Know You Got Soul", "Eric B. & Rakim"),
        [6] = track("Brand Nubian", "Brand Nubian"),
        [7] = track("Children's Story", "Slick Rick"),
        [8] = track("B.Y.S.", "Gang Starr"),
        [9] = track("Critical Beatdown", "Ultramagnetic MCs"),
        [10] = track("The Vapors", "Biz Markie"),
        [11] = track("The Godfather", "Spoonie Gee"),
    },
    [2] = {
        [0] = track("Amos Moses", "Jerry Reed"),
        [1] = track("Louisiana Woman, Mississippi Man", "Conway Twitty & Loretta Lynn"),
        [2] = track("One Step Forward", "The Desert Rose Band"),
        [3] = track("New York City", "The Statler Brothers"),
        [4] = track("Bed of Rose's", "The Statler Brothers"),
        [5] = track("The Letter That Johnny Walker Read", "Asleep at the Wheel"),
        [6] = track("Queen of Hearts", "Juice Newton"),
        [7] = track("Hey Good Lookin'", "Hank Williams"),
        [8] = track("Three Cigarettes in an Ashtray", "Patsy Cline"),
        [9] = track("I Love a Rainy Night", "Eddie Rabbitt"),
        [10] = track("Crazy", "Willie Nelson"),
        [11] = track("Make the World Go Away", "Mickey Gilley"),
        [12] = track("Mammas Don't Let Your Babies Grow Up to Be Cowboys", "Ed Bruce"),
        [13] = track("Always Wanting You", "Merle Haggard"),
        [14] = track("All My Ex's Live in Texas", "Whitey Shafer"),
    },
    [3] = {
        [0] = track("Runnin' Down a Dream", "Tom Petty"),
        [1] = track("Barracuda", "Heart"),
        [2] = track("Woman to Woman", "Joe Cocker"),
        [3] = track("Young Turks", "Rod Stewart"),
        [4] = track("Somebody Up There Likes Me", "David Bowie"),
        [5] = track("Some Kind of Wonderful", "Grand Funk Railroad"),
        [6] = track("Strutter", "Kiss"),
        [7] = track("Hold the Line", "Toto"),
        [8] = track("Green River", "Creedence Clearwater Revival"),
        [9] = track("Eminence Front", "The Who"),
        [10] = track("A Horse with No Name", "America"),
        [11] = track("Slow Ride", "Foghat"),
        [12] = track("White Wedding", "Billy Idol"),
        [13] = track("Get Down to It", "Humble Pie"),
        [14] = track("Two Tickets to Paradise", "Eddie Money"),
        [15] = track("Smokin'", "Boston"),
        [16] = track("Free Bird", "Lynyrd Skynyrd"),
    },
    [4] = {
        [0] = track("Love Rollercoaster", "Ohio Players"),
        [1] = track("Loopzilla", "George Clinton"),
        [2] = track("Candy", "Cameo"),
        [3] = track("You Dropped a Bomb on Me", "The Gap Band"),
        [4] = track("Cold Blooded", "Rick James"),
        [5] = track("I Can Make You Dance", "Zapp"),
        [6] = track("Hollywood Swinging", "Kool & the Gang"),
        [7] = track("Twilight", "Maze"),
        [8] = track("Yum Yum (Gimme Some)", "Fatback Band"),
        [9] = track("West Coast Poplock", "Ronnie Hudson and The Street People"),
        [10] = track("Fantastic Voyage", "Lakeside"),
        [11] = track("Let It Whip", "Dazz Band"),
        [12] = track("Between the Sheets", "The Isley Brothers"),
        [13] = track("Love Is the Message", "MFSB"),
        [14] = track("Funky Worm", "Ohio Players"),
        [15] = track("Running Away", "Roy Ayers"),
        [16] = track("Odyssey", "Johnny Harris"),
    },
    [5] = {
        [0] = track("Promised Land", "Joe Smooth feat. Anthony Thomas"),
        [1] = track("Pacific 202", "808 State"),
        [2] = track("Voodoo Ray", "A Guy Called Gerald"),
        [3] = track("Your Love", "Frankie Knuckles feat. Jamie Principle"),
        [4] = track("Break 4 Love", "Raze"),
        [5] = track("Ma Foom Bey", "Cultural Vibe"),
        [6] = track("Make My Body Rock", "Jomanda"),
        [7] = track("Someday", "CeCe Rogers"),
        [8] = track("Let the Music Use You", "Nightwriters"),
        [9] = track("Can You Feel It?", "Mr. Fingers"),
        [10] = track("Move Your Body", "Marshall Jefferson"),
        [11] = track("This Is Acid", "Maurice"),
        [12] = track("Weekend", "The Todd Terry Project"),
        [13] = track("The Morning After", "Fallout"),
        [14] = track("I'll Be Your Friend", "Robert Owens"),
        [15] = track("I Need a Rhythm", "The 28th Street Crew"),
    },
    [6] = {
        [0] = track("Fuck wit Dre Day", "Dr. Dre feat. Snoop Dogg"),
        [1] = track("I Don't Give a Fuck", "2Pac feat. Pogo"),
        [2] = track("Nuthin' but a 'G' Thang", "Dr. Dre feat. Snoop Dogg"),
        [3] = track("Hood Took Me Under", "Compton's Most Wanted"),
        [4] = track("It's Funky Enough", "The D.O.C."),
        [5] = track("Alwayz into Somethin'", "N.W.A."),
        [6] = track("Express Yourself", "N.W.A."),
        [7] = track("La Raza", "Kid Frost"),
        [8] = track("How I Could Just Kill a Man", "Cypress Hill"),
        [9] = track("Murder Rap", "Above the Law"),
        [10] = track("Eazy-Er Said Than Dunn", "Eazy-E"),
        [11] = track("Guerillas in tha Mist", "Da Lench Mob feat. Ice Cube"),
        [12] = track("It Was a Good Day", "Ice Cube"),
        [13] = track("Check Yo Self", "Ice Cube feat. Das EFX"),
        [14] = track("Deep Cover", "Dr. Dre feat. Snoop Dogg"),
        [15] = track("The Ghetto", "Too $hort"),
    },
    [7] = {
        [0] = track("Midlife Crisis", "Faith No More"),
        [1] = track("Movin' on Up", "Primal Scream"),
        [2] = track("Personal Jesus", "Depeche Mode"),
        [3] = track("Mother", "Danzig"),
        [4] = track("Unsung", "Helmet"),
        [5] = track("Cult of Personality", "Living Colour"),
        [6] = track("Hellraiser", "Ozzy Osbourne"),
        [7] = track("Killing in the Name", "Rage Against the Machine"),
        [8] = track("Welcome to the Jungle", "Guns N' Roses"),
        [9] = track("Been Caught Stealing", "Jane's Addiction"),
        [10] = track("Rusty Cage", "Soundgarden"),
        [11] = track("Pretend We're Dead", "L7"),
        [12] = track("Fools Gold", "The Stone Roses"),
        [13] = track("Them Bones", "Alice in Chains"),
        [14] = track("Plush", "Stone Temple Pilots"),
    },
    [8] = {
        [0] = track("Keep On Movin'", "Soul II Soul"),
        [1] = track("So You Like What You See", "Samuelle"),
        [2] = track("Sensitivity", "Ralph Tresvant"),
        [3] = track("My Lovin' (You're Never Gonna Get It)", "En Vogue"),
        [4] = track("I'm So Into You", "SWV"),
        [5] = track("Groove Me", "Guy"),
        [6] = track("Rub You the Right Way", "Johnny Gill"),
        [7] = track("Motownphilly", "Boyz II Men"),
        [8] = track("Don't Be Cruel", "Bobby Brown"),
        [9] = track("Don't Be Afraid", "Aaron Hall"),
        [10] = track("Poison", "Bell Biv DeVoe"),
        [11] = track("New Jack Swing", "Wreckx-n-Effect"),
        [12] = track("I Got the Feeling", "Today"),
    },
    [9] = {
        [0] = track("King Tubby Meets Rockers Uptown", "Augustus Pablo"),
        [1] = track("Funky Kingston", "Toots & The Maytals"),
        [2] = track("Ring My Bell", "Blood Sisters"),
        [3] = track("Don't Let It Go to Your Head", "Black Harmony"),
        [4] = track("Revolution", "Dennis Brown"),
        [5] = track("Sidewalk Killer", "I-Roy"),
        [6] = track("Wicked Inna Bed", "Shabba Ranks"),
        [7] = track("Batty Rider", "Buju Banton"),
        [8] = track("Cokane in My Brain", "Dillinger"),
        [9] = track("Armagideon Time", "Willi Williams"),
        [10] = track("Here I Come", "Barrington Levy"),
        [11] = track("Great Train Robbery", "Black Uhuru"),
        [12] = track("Drum Pan Sound", "Reggie Stepper"),
        [13] = track("Pressure Drop", "Toots & The Maytals"),
        [14] = track("Chase the Devil", "Max Romeo & The Upsetters"),
        [15] = track("Bam Bam", "Pliers"),
    },
    [10] = {
        [0] = track("Express Yourself", "Charles Wright & the Watts 103rd Street Rhythm Band"),
        [1] = track("Green Onions", "Booker T. & the M.G.'s"),
        [2] = track("Cross the Tracks", "Maceo & The Macks"),
        [3] = track("Hot Pants", "Bobby Byrd"),
        [4] = track("Think (About It)", "Lyn Collins"),
        [5] = track("Rock Creek Park", "The Blackbyrds"),
        [6] = track("Nautilus", "Bob James"),
        [7] = track("Funky President", "James Brown"),
        [8] = track("The Grunt", "The J.B.'s"),
        [9] = track("Jungle Fever", "The Chakachas"),
        [10] = track("Low Rider", "War"),
        [11] = track("Soul Power '74", "Maceo & The Macks"),
        [12] = track("Tainted Love", "Gloria Jones"),
        [13] = track("(I Got) So Much Trouble in My Mind", "Sir Joe Quarterman & Free Soul"),
        [14] = track("Rock Me Again and Again", "Lyn Collins"),
        [15] = track("I Know You Got Soul", "Bobby Byrd"),
        [16] = track("The Payback", "James Brown"),
        [17] = track("Smokin' Cheeba Cheeba", "Harlem Underground Band"),
    },
}

local function splitMark(mark)
    local first, second = mark:match("^([^\n]+)\n(.+)$")
    return first or mark, second
end

function createRadioSelectorLogo(channel)
    local station = RADIO_SELECTOR_STATIONS[channel]
    if not station or type(svgCreate) ~= "function" then
        return false
    end

    local firstLine, secondLine = splitMark(station.mark)
    local secondText = secondLine and ("<text x='128' y='160' text-anchor='middle' font-family='Arial Black,Arial,sans-serif' font-size='44' font-weight='900' fill='" .. station.accent .. "'>" .. secondLine .. "</text>") or ""
    local firstY = secondLine and 112 or 145
    local firstSize = #firstLine <= 3 and 82 or (#firstLine <= 6 and 54 or 38)

    local svg = ([[
        <svg xmlns="http://www.w3.org/2000/svg" width="256" height="256" viewBox="0 0 256 256">
            <defs>
                <linearGradient id="bg" x1="0" y1="0" x2="1" y2="1">
                    <stop offset="0" stop-color="%s"/>
                    <stop offset="1" stop-color="%s"/>
                </linearGradient>
            </defs>
            <rect width="256" height="256" fill="url(#bg)"/>
            <circle cx="202" cy="52" r="70" fill="none" stroke="%s" stroke-width="16" opacity="0.30"/>
            <circle cx="42" cy="218" r="92" fill="none" stroke="%s" stroke-width="12" opacity="0.20"/>
            <path d="M-20 196 L196 -20 M32 276 L276 32" stroke="%s" stroke-width="5" opacity="0.18"/>
            <rect x="17" y="17" width="222" height="222" fill="none" stroke="%s" stroke-width="3" opacity="0.78"/>
            <text x="128" y="%d" text-anchor="middle" font-family="Arial Black,Arial,sans-serif" font-size="%d" font-weight="900" fill="%s">%s</text>
            %s
            <rect x="28" y="205" width="200" height="4" fill="%s" opacity="0.86"/>
        </svg>
    ]]):format(station.primary, station.secondary, station.accent, station.accent, station.accent, station.accent, firstY, firstSize, station.accent, firstLine, secondText, station.accent)

    return svgCreate(256, 256, svg)
end
