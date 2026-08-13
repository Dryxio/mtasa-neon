-- Server-authoritative population state for the native traffic harness.
--
-- The immutable base is the stock main.scm initialization already embedded in
-- Game SA. Presets contain only the later campaign mutations, which keeps one
-- canonical copy of the 336-zone bootstrap while still giving every client an
-- explicit, versioned world state.

local BASELINE = "stock-main-scm-bootstrap-601def3b"
local GROVE = 2
local BALLAS = 1
local WEAPON_UNARMED = 0
local WEAPON_PISTOL = 22
local WEAPON_TEC9 = 32

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
    setGang(zones, "GAN1", GROVE, 10)
    setGang(zones, "GAN2", GROVE, 10)
    return zones
end

local function buildPostCleaningTheHood()
    local zones = {}
    setGang(zones, "GAN1", GROVE, 40)
    setGang(zones, "GAN2", GROVE, 40)
    return zones
end

local function buildPostGreenSabre()
    local zones = {}
    local territoryFamilies = {
        [BALLAS] = {
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
        setGang(zones, label, GROVE, 0)
    end
    -- Green Sabre explicitly clears Grove in the hospital sub-zone even though
    -- it deliberately assigns no replacement gang there.
    setGang(zones, "JEF3A", GROVE, 0)
    return zones
end

local function buildPostHomeComing()
    local zones = buildPostGreenSabre()
    setGang(zones, "GAN1", GROVE, 40)
    setGang(zones, "GAN2", GROVE, 40)
    setGang(zones, "GAN1", BALLAS, 0)
    setGang(zones, "GAN2", BALLAS, 0)
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
            gangWeapons = {[GROVE] = {WEAPON_PISTOL, WEAPON_TEC9, WEAPON_UNARMED}},
        }
    end,
    post_home_coming = function()
        return {
            zones = buildPostHomeComing(),
            gangWarsActive = true,
            gangWeapons = {[GROVE] = {WEAPON_PISTOL, WEAPON_TEC9, WEAPON_UNARMED}},
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
    self.gangWeapons = preset.gangWeapons or {}
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
