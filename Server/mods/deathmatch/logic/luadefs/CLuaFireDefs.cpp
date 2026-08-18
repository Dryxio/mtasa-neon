/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  PURPOSE:     Managed fire Lua definitions
 *
 *****************************************************************************/

#include "StdInc.h"
#include "CLuaFireDefs.h"
#include "../CDummy.h"
#include "../CElementGroup.h"
#include "../CResource.h"
#include "../packets/CEntityAddPacket.h"
#include "../CStaticFunctionDefinitions.h"
#include <CScriptArgReader.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <vector>

namespace
{
constexpr unsigned char DAMAGE_PLAYERS = 1 << 0;
constexpr unsigned char DAMAGE_PEDS = 1 << 1;
constexpr unsigned char DAMAGE_VEHICLES = 1 << 2;
constexpr unsigned char DAMAGE_OBJECTS = 1 << 3;
constexpr unsigned char DAMAGE_ALL = DAMAGE_PLAYERS | DAMAGE_PEDS | DAMAGE_VEHICLES | DAMAGE_OBJECTS;

constexpr const char* KEY_DURATION = "__neon_fire_duration";
constexpr const char* KEY_REMAINING = "__neon_fire_remaining";
constexpr const char* KEY_EXPIRY = "__neon_fire_expiry";  // Server-local only; never sent to clients.
constexpr const char* KEY_STRENGTH = "__neon_fire_strength";
constexpr const char* KEY_DAMAGE = "__neon_fire_damage";
constexpr const char* KEY_DAMAGE_MASK = "__neon_fire_damage_mask";
constexpr const char* KEY_SPREAD = "__neon_fire_spread";
constexpr const char* KEY_MAX_GENERATIONS = "__neon_fire_max_generations";
constexpr const char* KEY_GENERATION = "__neon_fire_generation";
constexpr const char* KEY_SOURCE = "__neon_fire_source";
constexpr const char* KEY_TARGET = "__neon_fire_target";

struct SFireRecord
{
    CDummy*    pFire{};
    CResource* pResource{};
    double     dNextSpread{};
    bool       bDidSpread{};
};

std::unordered_map<CDummy*, SFireRecord> g_Fires;

double NowMs()
{
    return static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

bool IsFire(CElement* pElement)
{
    return pElement && pElement->GetType() == CElement::DUMMY && pElement->GetTypeName() == "fire";
}

void StoreNumber(CElement* pFire, const char* key, double value, ESyncType syncType)
{
    CLuaArgument argument;
    argument.ReadNumber(value);
    pFire->GetCustomDataManager().Set(key, argument, syncType);
}

void SetInitialNumber(CElement* pFire, const char* key, double value)
{
    StoreNumber(pFire, key, value, ESyncType::BROADCAST);
}

void SetInitialBool(CElement* pFire, const char* key, bool value)
{
    CLuaArgument argument;
    argument.ReadBool(value);
    pFire->GetCustomDataManager().Set(key, argument, ESyncType::BROADCAST);
}

void SetInitialElement(CElement* pFire, const char* key, CElement* value)
{
    if (!value)
        return;

    CLuaArgument argument;
    argument.ReadElement(value);
    pFire->GetCustomDataManager().Set(key, argument, ESyncType::BROADCAST);
}

void SetNumber(CElement* pFire, const char* key, double value)
{
    CLuaArgument argument;
    argument.ReadNumber(value);
    CStaticFunctionDefinitions::SetElementData(pFire, key, argument, ESyncType::BROADCAST, std::nullopt);
}

void SetBool(CElement* pFire, const char* key, bool value)
{
    CLuaArgument argument;
    argument.ReadBool(value);
    CStaticFunctionDefinitions::SetElementData(pFire, key, argument, ESyncType::BROADCAST, std::nullopt);
}

void SetElement(CElement* pFire, const char* key, CElement* value)
{
    if (!value)
    {
        CStaticFunctionDefinitions::RemoveElementData(pFire, key);
        return;
    }

    CLuaArgument argument;
    argument.ReadElement(value);
    CStaticFunctionDefinitions::SetElementData(pFire, key, argument, ESyncType::BROADCAST, std::nullopt);
}

bool GetNumber(CElement* pFire, const char* key, double& value)
{
    CLuaArgument* argument = pFire ? pFire->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TNUMBER)
        return false;

    value = argument->GetNumber();
    return true;
}

bool GetBool(CElement* pFire, const char* key, bool& value)
{
    CLuaArgument* argument = pFire ? pFire->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TBOOLEAN)
        return false;

    value = argument->GetBoolean();
    return true;
}

CElement* GetElement(CElement* pFire, const char* key)
{
    CLuaArgument* argument = pFire ? pFire->GetCustomData(key, false) : nullptr;
    return argument ? argument->GetElement() : nullptr;
}

bool ReadTableBool(lua_State* luaVM, int tableIndex, const char* key, bool defaultValue)
{
    lua_getfield(luaVM, tableIndex, key);
    const bool value = lua_isboolean(luaVM, -1) ? lua_toboolean(luaVM, -1) != 0 : defaultValue;
    lua_pop(luaVM, 1);
    return value;
}

double ReadTableNumber(lua_State* luaVM, int tableIndex, const char* key, double defaultValue)
{
    lua_getfield(luaVM, tableIndex, key);
    const double value = lua_isnumber(luaVM, -1) ? lua_tonumber(luaVM, -1) : defaultValue;
    lua_pop(luaVM, 1);
    return value;
}

CElement* ReadTableElement(lua_State* luaVM, int tableIndex, const char* key)
{
    lua_getfield(luaVM, tableIndex, key);
    CElement* value = lua_toelement(luaVM, -1);
    lua_pop(luaVM, 1);
    return value;
}

unsigned char ReadDamageMask(lua_State* luaVM, int optionsIndex)
{
    unsigned char mask = DAMAGE_ALL;
    lua_getfield(luaVM, optionsIndex, "damageTargets");
    if (lua_istable(luaVM, -1))
    {
        const int table = lua_gettop(luaVM);
        mask = 0;
        if (ReadTableBool(luaVM, table, "players", true))
            mask |= DAMAGE_PLAYERS;
        if (ReadTableBool(luaVM, table, "peds", true))
            mask |= DAMAGE_PEDS;
        if (ReadTableBool(luaVM, table, "vehicles", true))
            mask |= DAMAGE_VEHICLES;
        if (ReadTableBool(luaVM, table, "objects", true))
            mask |= DAMAGE_OBJECTS;
    }
    lua_pop(luaVM, 1);
    return mask;
}

void PushDamageMask(lua_State* luaVM, unsigned char mask)
{
    lua_createtable(luaVM, 0, 4);
    lua_pushboolean(luaVM, (mask & DAMAGE_PLAYERS) != 0);
    lua_setfield(luaVM, -2, "players");
    lua_pushboolean(luaVM, (mask & DAMAGE_PEDS) != 0);
    lua_setfield(luaVM, -2, "peds");
    lua_pushboolean(luaVM, (mask & DAMAGE_VEHICLES) != 0);
    lua_setfield(luaVM, -2, "vehicles");
    lua_pushboolean(luaVM, (mask & DAMAGE_OBJECTS) != 0);
    lua_setfield(luaVM, -2, "objects");
}

CDummy* CreateFireElement(CResource* pResource, const CVector& position, double duration, double strength, bool damage, unsigned char mask, bool spread,
                          unsigned int maxGenerations, unsigned int generation, CElement* source, CElement* target, unsigned short dimension = 0,
                          unsigned char interior = 0)
{
    if (!pResource)
        return nullptr;

    CDummy* fire = new CDummy(g_pGame->GetGroups(), pResource->GetDynamicElementRoot());
    fire->SetTypeName("fire");
    fire->SetPosition(position);
    fire->SetDimension(dimension);
    fire->SetInterior(interior);

    // Seed all synchronized values before EntityAdd. The absolute expiry remains local
    // to the server; clients only receive a relative remaining duration so their wall
    // clocks never need to agree with the server clock.
    SetInitialNumber(fire, KEY_DURATION, duration);
    SetInitialNumber(fire, KEY_REMAINING, duration);
    StoreNumber(fire, KEY_EXPIRY, duration > 0.0 ? NowMs() + duration : 0.0, ESyncType::LOCAL);
    SetInitialNumber(fire, KEY_STRENGTH, strength);
    SetInitialBool(fire, KEY_DAMAGE, damage);
    SetInitialNumber(fire, KEY_DAMAGE_MASK, mask);
    SetInitialBool(fire, KEY_SPREAD, spread);
    SetInitialNumber(fire, KEY_MAX_GENERATIONS, maxGenerations);
    SetInitialNumber(fire, KEY_GENERATION, generation);
    SetInitialElement(fire, KEY_SOURCE, source);
    SetInitialElement(fire, KEY_TARGET, target);

    if (CElementGroup* group = pResource->GetElementGroup())
        group->Add(fire);

    g_Fires.emplace(fire, SFireRecord{fire, pResource, NowMs() + 1500.0, false});

    if (pResource->IsClientSynced())
    {
        CEntityAddPacket packet;
        packet.Add(fire);
        g_pGame->GetPlayerManager()->BroadcastOnlyJoined(packet);
    }

    return fire;
}

bool DestroyManagedFire(CDummy* fire)
{
    if (!fire || !IsFire(fire))
        return false;

    g_Fires.erase(fire);
    return CStaticFunctionDefinitions::DestroyElement(fire);
}

void UpdateSpreadReset(CElement* fire)
{
    auto iter = g_Fires.find(static_cast<CDummy*>(fire));
    if (iter != g_Fires.end())
    {
        iter->second.bDidSpread = false;
        iter->second.dNextSpread = NowMs() + 1500.0;
    }
}
}  // namespace

void CLuaFireDefs::LoadFunctions()
{
    constexpr static const std::pair<const char*, lua_CFunction> functions[]{
        {"createFire", CreateFire},
        {"extinguishFire", ExtinguishFire},
        {"getFireDuration", GetFireDuration},
        {"setFireDuration", SetFireDuration},
        {"getFireRemainingTime", GetFireRemainingTime},
        {"setFireRemainingTime", SetFireRemainingTime},
        {"getFireStrength", GetFireStrength},
        {"setFireStrength", SetFireStrength},
        {"isFireDamageEnabled", IsFireDamageEnabled},
        {"setFireDamageEnabled", SetFireDamageEnabled},
        {"getFireDamageTargets", GetFireDamageTargets},
        {"setFireDamageTargets", SetFireDamageTargets},
        {"isFireSpreadEnabled", IsFireSpreadEnabled},
        {"setFireSpreadEnabled", SetFireSpreadEnabled},
        {"getFireMaxGenerations", GetFireMaxGenerations},
        {"setFireMaxGenerations", SetFireMaxGenerations},
        {"getFireSource", GetFireSource},
        {"setFireSource", SetFireSource},
        {"getFireTarget", GetFireTarget},
        {"setFireTarget", SetFireTarget},
    };

    for (const auto& [name, func] : functions)
        CLuaCFunctions::AddFunction(name, func);
}

void CLuaFireDefs::OnFireDestroyed(CDummy* pFire)
{
    g_Fires.erase(pFire);
}

void CLuaFireDefs::DoPulse()
{
    const double now = NowMs();
    std::vector<CDummy*> expired;
    std::vector<SFireRecord> spreadCandidates;

    for (auto& [fire, record] : g_Fires)
    {
        if (!fire || fire->IsBeingDeleted())
            continue;

        double duration = 0.0;
        double expiry = 0.0;
        GetNumber(fire, KEY_DURATION, duration);
        GetNumber(fire, KEY_EXPIRY, expiry);
        if (duration > 0.0)
        {
            const double remaining = expiry > 0.0 ? std::max(0.0, expiry - now) : 0.0;
            // This updates the authoritative stored value without emitting a packet every
            // pulse. Existing clients count down locally; a late join gets this fresh value
            // in its EntityAdd custom-data snapshot.
            StoreNumber(fire, KEY_REMAINING, remaining, ESyncType::BROADCAST);
            if (remaining <= 0.0)
            {
                expired.push_back(fire);
                continue;
            }
        }

        bool spread = false;
        double generation = 0.0;
        double maxGenerations = 0.0;
        GetBool(fire, KEY_SPREAD, spread);
        GetNumber(fire, KEY_GENERATION, generation);
        GetNumber(fire, KEY_MAX_GENERATIONS, maxGenerations);
        if (spread && !record.bDidSpread && generation < maxGenerations && now >= record.dNextSpread)
        {
            record.bDidSpread = true;
            spreadCandidates.push_back(record);
        }
    }

    for (CDummy* fire : expired)
        DestroyManagedFire(fire);

    for (const SFireRecord& record : spreadCandidates)
    {
        CDummy* parent = record.pFire;
        if (!parent || parent->IsBeingDeleted() || !record.pResource)
            continue;

        double generation = 0.0;
        double maxGenerations = 0.0;
        double strength = 1.0;
        double mask = DAMAGE_ALL;
        bool damage = true;
        bool spread = true;
        GetNumber(parent, KEY_GENERATION, generation);
        GetNumber(parent, KEY_MAX_GENERATIONS, maxGenerations);
        GetNumber(parent, KEY_STRENGTH, strength);
        GetNumber(parent, KEY_DAMAGE_MASK, mask);
        GetBool(parent, KEY_DAMAGE, damage);
        GetBool(parent, KEY_SPREAD, spread);

        const double angle = static_cast<double>(std::rand() % 6284) / 1000.0;
        const double distance = 2.0 + static_cast<double>(std::rand() % 1000) / 1000.0;
        CVector position = parent->GetPosition();
        position.fX += static_cast<float>(std::cos(angle) * distance);
        position.fY += static_cast<float>(std::sin(angle) * distance);

        CreateFireElement(record.pResource, position, 20000.0, std::max(0.8, strength * 0.8), damage,
                          static_cast<unsigned char>(static_cast<unsigned int>(mask)) & DAMAGE_ALL, spread,
                          static_cast<unsigned int>(maxGenerations), static_cast<unsigned int>(generation) + 1, GetElement(parent, KEY_SOURCE), nullptr,
                          parent->GetDimension(), parent->GetInterior());
    }
}

int CLuaFireDefs::CreateFire(lua_State* luaVM)
{
    CVector position;
    CScriptArgReader args(luaVM);
    args.ReadVector3D(position);
    if (args.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, args.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    double duration = 5000.0;
    double strength = 1.0;
    bool damage = true;
    bool spread = false;
    unsigned int maxGenerations = 0;
    unsigned char damageMask = DAMAGE_ALL;
    CElement* source = nullptr;
    CElement* target = nullptr;

    if (lua_istable(luaVM, 4))
    {
        duration = std::max(0.0, ReadTableNumber(luaVM, 4, "duration", duration));
        strength = std::max(0.1, ReadTableNumber(luaVM, 4, "strength", strength));
        damage = ReadTableBool(luaVM, 4, "damage", damage);
        spread = ReadTableBool(luaVM, 4, "spread", spread);
        maxGenerations = static_cast<unsigned int>(std::clamp(ReadTableNumber(luaVM, 4, "maxGenerations", 0.0), 0.0, 255.0));
        damageMask = ReadDamageMask(luaVM, 4);
        source = ReadTableElement(luaVM, 4, "source");
        target = ReadTableElement(luaVM, 4, "target");
    }
    else if (!lua_isnoneornil(luaVM, 4))
    {
        if (!lua_isnumber(luaVM, 4))
        {
            lua_pushboolean(luaVM, false);
            return 1;
        }
        strength = std::max(0.1, static_cast<double>(lua_tonumber(luaVM, 4)));
    }

    CLuaMain* luaMain = m_pLuaManager->GetVirtualMachine(luaVM);
    CResource* resource = luaMain ? luaMain->GetResource() : nullptr;
    CDummy* fire = CreateFireElement(resource, position, duration, strength, damage, damageMask, spread, maxGenerations, 0, source, target);
    if (fire)
        lua_pushelement(luaVM, fire);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::ExtinguishFire(lua_State* luaVM)
{
    CElement* firstElement = lua_toelement(luaVM, 1);
    if (IsFire(firstElement))
    {
        lua_pushboolean(luaVM, DestroyManagedFire(static_cast<CDummy*>(firstElement)));
        return 1;
    }

    if (lua_gettop(luaVM) == 0)
    {
        std::vector<CDummy*> fires;
        fires.reserve(g_Fires.size());
        for (const auto& [fire, record] : g_Fires)
            fires.push_back(fire);
        for (CDummy* fire : fires)
            DestroyManagedFire(fire);
        lua_pushboolean(luaVM, true);
        return 1;
    }

    CVector position;
    float radius = 1.0f;
    CScriptArgReader args(luaVM);
    args.ReadVector3D(position);
    args.ReadNumber(radius, 1.0f);
    if (args.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, args.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    const float radiusSq = radius * radius;
    std::vector<CDummy*> fires;
    for (const auto& [fire, record] : g_Fires)
    {
        const CVector& firePosition = fire->GetPosition();
        const float dx = firePosition.fX - position.fX;
        const float dy = firePosition.fY - position.fY;
        const float dz = firePosition.fZ - position.fZ;
        if (dx * dx + dy * dy + dz * dz <= radiusSq)
            fires.push_back(fire);
    }
    for (CDummy* fire : fires)
        DestroyManagedFire(fire);
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaFireDefs::GetFireDuration(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    double value = 0.0;
    if (IsFire(fire) && GetNumber(fire, KEY_DURATION, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDuration(lua_State* luaVM)
{
    CElement* fire = nullptr;
    double duration = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(duration);
    if (!args.HasErrors() && IsFire(fire) && duration >= 0.0)
    {
        SetNumber(fire, KEY_DURATION, duration);
        SetNumber(fire, KEY_REMAINING, duration);
        StoreNumber(fire, KEY_EXPIRY, duration > 0.0 ? NowMs() + duration : 0.0, ESyncType::LOCAL);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireRemainingTime(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    double duration = 0.0;
    double expiry = 0.0;
    if (IsFire(fire) && GetNumber(fire, KEY_DURATION, duration) && GetNumber(fire, KEY_EXPIRY, expiry))
    {
        lua_pushnumber(luaVM, duration <= 0.0 ? 0.0 : std::max(0.0, expiry - NowMs()));
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireRemainingTime(lua_State* luaVM)
{
    CElement* fire = nullptr;
    double remaining = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(remaining);
    if (!args.HasErrors() && IsFire(fire) && remaining >= 0.0)
    {
        double duration = 0.0;
        GetNumber(fire, KEY_DURATION, duration);
        if (duration <= 0.0 && remaining > 0.0)
        {
            duration = remaining;
            SetNumber(fire, KEY_DURATION, duration);
        }
        SetNumber(fire, KEY_REMAINING, remaining);
        StoreNumber(fire, KEY_EXPIRY, remaining > 0.0 ? NowMs() + remaining : (duration > 0.0 ? NowMs() : 0.0), ESyncType::LOCAL);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireStrength(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    double value = 0.0;
    if (IsFire(fire) && GetNumber(fire, KEY_STRENGTH, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireStrength(lua_State* luaVM)
{
    CElement* fire = nullptr;
    double strength = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(strength);
    if (!args.HasErrors() && IsFire(fire) && strength > 0.0)
    {
        SetNumber(fire, KEY_STRENGTH, strength);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::IsFireDamageEnabled(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    bool value = false;
    if (IsFire(fire) && GetBool(fire, KEY_DAMAGE, value))
        lua_pushboolean(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDamageEnabled(lua_State* luaVM)
{
    CElement* fire = nullptr;
    bool enabled = false;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadBool(enabled);
    if (!args.HasErrors() && IsFire(fire))
    {
        SetBool(fire, KEY_DAMAGE, enabled);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireDamageTargets(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    double value = DAMAGE_ALL;
    if (IsFire(fire))
    {
        GetNumber(fire, KEY_DAMAGE_MASK, value);
        PushDamageMask(luaVM, static_cast<unsigned char>(static_cast<unsigned int>(value)) & DAMAGE_ALL);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDamageTargets(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    if (!IsFire(fire) || !lua_istable(luaVM, 2))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    unsigned char mask = 0;
    if (ReadTableBool(luaVM, 2, "players", true))
        mask |= DAMAGE_PLAYERS;
    if (ReadTableBool(luaVM, 2, "peds", true))
        mask |= DAMAGE_PEDS;
    if (ReadTableBool(luaVM, 2, "vehicles", true))
        mask |= DAMAGE_VEHICLES;
    if (ReadTableBool(luaVM, 2, "objects", true))
        mask |= DAMAGE_OBJECTS;
    SetNumber(fire, KEY_DAMAGE_MASK, mask);
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaFireDefs::IsFireSpreadEnabled(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    bool value = false;
    if (IsFire(fire) && GetBool(fire, KEY_SPREAD, value))
        lua_pushboolean(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireSpreadEnabled(lua_State* luaVM)
{
    CElement* fire = nullptr;
    bool enabled = false;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadBool(enabled);
    if (!args.HasErrors() && IsFire(fire))
    {
        SetBool(fire, KEY_SPREAD, enabled);
        UpdateSpreadReset(fire);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireMaxGenerations(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    double value = 0.0;
    if (IsFire(fire) && GetNumber(fire, KEY_MAX_GENERATIONS, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireMaxGenerations(lua_State* luaVM)
{
    CElement* fire = nullptr;
    unsigned int generations = 0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(generations);
    if (!args.HasErrors() && IsFire(fire) && generations <= 255)
    {
        SetNumber(fire, KEY_MAX_GENERATIONS, generations);
        UpdateSpreadReset(fire);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireSource(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    if (IsFire(fire))
    {
        CElement* value = GetElement(fire, KEY_SOURCE);
        if (value)
            lua_pushelement(luaVM, value);
        else
            lua_pushboolean(luaVM, false);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireSource(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    CElement* value = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    if (IsFire(fire) && (lua_isnoneornil(luaVM, 2) || value))
    {
        SetElement(fire, KEY_SOURCE, value);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireTarget(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    if (IsFire(fire))
    {
        CElement* value = GetElement(fire, KEY_TARGET);
        if (value)
            lua_pushelement(luaVM, value);
        else
            lua_pushboolean(luaVM, false);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireTarget(lua_State* luaVM)
{
    CElement* fire = lua_toelement(luaVM, 1);
    CElement* value = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    if (IsFire(fire) && (lua_isnoneornil(luaVM, 2) || value))
    {
        SetElement(fire, KEY_TARGET, value);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}