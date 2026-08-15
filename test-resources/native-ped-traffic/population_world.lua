-- Server-authoritative population state for the native traffic harness.
--
-- The immutable base is the stock main.scm initialization already embedded in
-- Game SA. Presets contain only the later campaign mutations, which keeps one
-- canonical copy of the 336-zone bootstrap while still giving every client an
-- explicit, versioned world state.

local BASELINE = "stock-main-scm-bootstrap-601def3b"
-- main.scm zone strengths are 1-based family columns, while CGangs' runtime
-- weapon table is 0-based. Keep the domains explicit so a story mutation can
-- never silently write Vagos weapons while changing Grove territory.
local GROVE_ZONE_INDEX = 2
local BALLAS_ZONE_INDEX = 1
local GROVE_GANG_ID = 1
local WEAPON_UNARMED = 0
local WEAPON_PISTOL = 22
local WEAPON_TEC9 = 32

-- CGangs::Initialise (GTA SA 1.0 US 0x5DE680). Ambient AddPed reads these
-- three runtime slots; main.scm's SET_GANG_WEAPONS opcode mutates the same
-- table later in the campaign. There is no gangs.dat weapon source in SA.
local BASE_GANG_WEAPONS = {
    [0] = {22, 28, 0}, -- Ballas: pistol / micro uzi
    [1] = {22, 0, 0},  -- Grove
    [2] = {22, 0, 0},  -- Vagos
    [3] = {0, 0, 0},   -- Rifa
    [4] = {22, 28, 0}, -- Da Nang Boys
    [5] = {24, 0, 0},  -- Mafia: desert eagle
    [6] = {22, 30, 0}, -- Triads: pistol / AK-47
    [7] = {22, 28, 0}, -- Aztecas
    [8] = {0, 0, 0},   -- unused Russian slot
    [9] = {0, 0, 0},   -- unused Bikers slot
}

local function setGang(zoneStates, label, gangIndex, strength)
    local state = zoneStates[label]
    if not state then
        state = {gangStrengths = {}}
        zoneStates[label] = state
    end
    state.gangStrengths[gangIndex] = strength
end

local function buildPostIntro()
    local zones = {}
    setGang(zones, "GAN1", GROVE_ZONE_INDEX, 10)
    setGang(zones, "GAN2", GROVE_ZONE_INDEX, 10)
    return zones
end

local function buildPostCleaningTheHood()
    local zones = {}
    setGang(zones, "GAN1", GROVE_ZONE_INDEX, 40)
    setGang(zones, "GAN2", GROVE_ZONE_INDEX, 40)
    return zones
end

local function buildPostGreenSabre()
    local zones = {}
    local territoryFamilies = {
        [BALLAS_ZONE_INDEX] = {
            SUN1 = 30, SUN3A = 30, SUN3B = 30, SUN3C = 30, SUN4 = 30,
            GAN1 = 10, GAN2 = 25, GLN1 = 40, GLN2A = 40,
            LIND1A = 20, LIND1B = 20, LIND2A = 20, LIND2B = 20, LIND3 = 20,
            IWD1 = 20, IWD2 = 20, IWD3A = 20, IWD3B = 20, IWD4 = 10, IWD5 = 20,
            JEF1A = 40, JEF1B = 40, JEF2 = 40, JEF3B = 40, JEF3C = 40,
            ELS1A = 30, ELS1B = 30, ELS2 = 30, ELS3A = 30, ELS3B = 30, ELS4 = 30,
            PLS = 10, SMB1 = 10, SMB2 = 10, VIN2 = 10,
            VERO1 = 10, VERO2 = 10, VERO3 = 10, VERO4A = 10, VERO4B = 10,
        },
        [3] = {
            CHC1A = 40, CHC1B = 40, CHC2A = 40, CHC2B = 40, CHC3 = 40, CHC4A = 40, CHC4B = 40,
            EBE1 = 30, EBE2A = 30, EBE2B = 30, EBE3C = 30,
            LFL1A = 40, LFL1B = 40,
        },
        [8] = {ELCO1 = 40, ELCO2 = 40, LMEX1A = 30, LMEX1B = 30},
    }
    local allTerritories = {}
    for gangIndex, territories in pairs(territoryFamilies) do
        for label, strength in pairs(territories) do
            setGang(zones, label, gangIndex, strength)
            allTerritories[label] = true
        end
    end
    for label in pairs(allTerritories) do
        setGang(zones, label, GROVE_ZONE_INDEX, 0)
    end
    -- Green Sabre explicitly clears Grove in the hospital sub-zone even though
    -- it deliberately assigns no replacement gang there.
    setGang(zones, "JEF3A", GROVE_ZONE_INDEX, 0)
    return zones
end

local function buildPostHomeComing()
    local zones = buildPostGreenSabre()
    setGang(zones, "GAN1", GROVE_ZONE_INDEX, 40)
    setGang(zones, "GAN2", GROVE_ZONE_INDEX, 40)
    setGang(zones, "GAN1", BALLAS_ZONE_INDEX, 0)
    setGang(zones, "GAN2", BALLAS_ZONE_INDEX, 0)
    return zones
end

local presetBuilders = {
    post_intro = function()
        return {
            zones = buildPostIntro(),
            gangWarsActive = false,
        }
    end,
    post_cleaning_the_hood = function()
        return {
            zones = buildPostCleaningTheHood(),
            gangWarsActive = false,
        }
    end,
    post_green_sabre = function()
        return {
            zones = buildPostGreenSabre(),
            gangWarsActive = false,
            gangWeapons = {[GROVE_GANG_ID] = {WEAPON_PISTOL, WEAPON_TEC9, WEAPON_UNARMED}},
        }
    end,
    post_home_coming = function()
        return {
            zones = buildPostHomeComing(),
            gangWarsActive = true,
            gangWeapons = {[GROVE_GANG_ID] = {WEAPON_PISTOL, WEAPON_TEC9, WEAPON_UNARMED}},
        }
    end,
}

local aliases = {
    intro = "post_intro",
    cleaning = "post_cleaning_the_hood",
    green_sabre = "post_green_sabre",
    home_coming = "post_home_coming",
}

local function deepCopy(value)
    if type(value) ~= "table" then
        return value
    end
    local copy = {}
    for key, child in pairs(value) do
        copy[deepCopy(key)] = deepCopy(child)
    end
    return copy
end

PedTrafficPopulationWorld = {}
PedTrafficPopulationWorld.__index = PedTrafficPopulationWorld

function PedTrafficPopulationWorld.create(initialPreset)
    local self = setmetatable({revision = 0}, PedTrafficPopulationWorld)
    assert(self:setPreset(initialPreset or "post_intro"))
    return self
end

function PedTrafficPopulationWorld:resolvePreset(name)
    name = tostring(name or ""):lower()
    return aliases[name] or name
end

function PedTrafficPopulationWorld:setPreset(name)
    name = self:resolvePreset(name)
    local builder = presetBuilders[name]
    if not builder then
        return false
    end

    local preset = builder()
    self.revision = self.revision + 1
    self.preset = name
    self.zones = preset.zones
    self.densityMultiplier = 1.0
    self.randomGangMembers = true
    self.riots = false
    self.gangWarsActive = preset.gangWarsActive == true
    self.gangWeapons = deepCopy(BASE_GANG_WEAPONS)
    for gang, slots in pairs(preset.gangWeapons or {}) do
        self.gangWeapons[gang] = deepCopy(slots)
    end
    return true
end

function PedTrafficPopulationWorld:getSnapshot()
    return deepCopy({
        schema = 1,
        baseline = BASELINE,
        revision = self.revision,
        preset = self.preset,
        densityMultiplier = self.densityMultiplier,
        randomGangMembers = self.randomGangMembers,
        riots = self.riots,
        gangWarsActive = self.gangWarsActive,
        gangWeapons = self.gangWeapons,
        zones = self.zones,
    })
end

function PedTrafficPopulationWorld:getClientProjection()
    return deepCopy({
        schema = 1,
        baseline = BASELINE,
        revision = self.revision,
        preset = self.preset,
        capabilities = {zones = true},
        zones = self.zones,
    })
end

function PedTrafficPopulationWorld:listPresets()
    return {
        "post_intro",
        "post_cleaning_the_hood",
        "post_green_sabre",
        "post_home_coming",
    }
end
