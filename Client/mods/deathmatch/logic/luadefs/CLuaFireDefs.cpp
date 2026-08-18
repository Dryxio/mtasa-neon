/*****************************************************************************
 *
 *  PROJECT:     Multi Theft Auto
 *  LICENSE:     See LICENSE in the top level directory
 *  FILE:        Client/mods/deathmatch/logic/luadefs/CLuaFireDefs.cpp
 *
 *  Multi Theft Auto is available from https://www.multitheftauto.com/
 *
 *****************************************************************************/
#include "StdInc.h"
#include "CLuaFireDefs.h"
#include "../CClientFireManager.h"
#include <chrono>

namespace
{
using FireManager = CClientFireManager;

long long NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

bool IsFire(CClientEntity* pElement)
{
    return FireManager::IsFireElement(pElement);
}

void SetNumber(CClientEntity* pFire, const char* key, double value)
{
    CLuaArgument argument;
    argument.ReadNumber(value);
    pFire->SetCustomData(key, argument, false);
}

void SetBool(CClientEntity* pFire, const char* key, bool value)
{
    CLuaArgument argument;
    argument.ReadBool(value);
    pFire->SetCustomData(key, argument, false);
}

void SetElement(CClientEntity* pFire, const char* key, CClientEntity* pValue)
{
    if (!pValue)
    {
        pFire->DeleteCustomData(key);
        return;
    }

    CLuaArgument argument;
    argument.ReadElement(pValue);
    pFire->SetCustomData(key, argument, false);
}

bool GetNumber(CClientEntity* pFire, const char* key, double& value)
{
    CLuaArgument* argument = pFire ? pFire->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TNUMBER)
        return false;
    value = argument->GetNumber();
    return true;
}

bool GetBool(CClientEntity* pFire, const char* key, bool& value)
{
    CLuaArgument* argument = pFire ? pFire->GetCustomData(key, false) : nullptr;
    if (!argument || argument->GetType() != LUA_TBOOLEAN)
        return false;
    value = argument->GetBoolean();
    return true;
}

CClientEntity* GetElement(CClientEntity* pFire, const char* key)
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

CClientEntity* ReadTableElement(lua_State* luaVM, int tableIndex, const char* key)
{
    lua_getfield(luaVM, tableIndex, key);
    CClientEntity* value = lua_toelement(luaVM, -1);
    lua_pop(luaVM, 1);
    return value;
}

unsigned char ReadDamageMask(lua_State* luaVM, int optionsIndex)
{
    unsigned char mask = FireManager::DAMAGE_ALL;
    lua_getfield(luaVM, optionsIndex, "damageTargets");
    if (lua_istable(luaVM, -1))
    {
        const int table = lua_gettop(luaVM);
        mask = 0;
        if (ReadTableBool(luaVM, table, "players", true))
            mask |= FireManager::DAMAGE_PLAYERS;
        if (ReadTableBool(luaVM, table, "peds", true))
            mask |= FireManager::DAMAGE_PEDS;
        if (ReadTableBool(luaVM, table, "vehicles", true))
            mask |= FireManager::DAMAGE_VEHICLES;
        if (ReadTableBool(luaVM, table, "objects", true))
            mask |= FireManager::DAMAGE_OBJECTS;
    }
    lua_pop(luaVM, 1);
    return mask;
}

void PushDamageMask(lua_State* luaVM, unsigned char mask)
{
    lua_createtable(luaVM, 0, 4);
    lua_pushboolean(luaVM, (mask & FireManager::DAMAGE_PLAYERS) != 0);
    lua_setfield(luaVM, -2, "players");
    lua_pushboolean(luaVM, (mask & FireManager::DAMAGE_PEDS) != 0);
    lua_setfield(luaVM, -2, "peds");
    lua_pushboolean(luaVM, (mask & FireManager::DAMAGE_VEHICLES) != 0);
    lua_setfield(luaVM, -2, "vehicles");
    lua_pushboolean(luaVM, (mask & FireManager::DAMAGE_OBJECTS) != 0);
    lua_setfield(luaVM, -2, "objects");
}

bool ReadFire(lua_State* luaVM, CClientEntity*& pFire)
{
    CScriptArgReader argStream(luaVM);
    argStream.ReadUserData(pFire);
    return !argStream.HasErrors() && IsFire(pFire);
}

bool CanMutate(CClientEntity* pFire)
{
    return pFire && pFire->IsLocalEntity();
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

int CLuaFireDefs::CreateFire(lua_State* luaVM)
{
    CVector vecPosition;
    CScriptArgReader argStream(luaVM);
    argStream.ReadVector3D(vecPosition);

    if (argStream.HasErrors())
    {
        m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
        lua_pushboolean(luaVM, false);
        return 1;
    }

    // Keep the historical numeric form byte-for-byte compatible: it creates a native,
    // unmanaged GTA fire and returns a boolean. The options-table form creates a managed
    // fire element with stable lifetime and control state.
    if (!lua_istable(luaVM, 4))
    {
        float size = 1.8f;
        if (!lua_isnoneornil(luaVM, 4))
        {
            if (!lua_isnumber(luaVM, 4))
            {
                lua_pushboolean(luaVM, false);
                return 1;
            }
            size = static_cast<float>(lua_tonumber(luaVM, 4));
        }
        lua_pushboolean(luaVM, CStaticFunctionDefinitions::CreateFire(vecPosition, size));
        return 1;
    }

    const int options = 4;
    const double duration = std::max(0.0, ReadTableNumber(luaVM, options, "duration", 5000.0));
    const double strength = std::max(0.1, ReadTableNumber(luaVM, options, "strength", 1.0));
    const bool damage = ReadTableBool(luaVM, options, "damage", true);
    const bool spread = ReadTableBool(luaVM, options, "spread", false);
    const double maxGenerations = std::max(0.0, ReadTableNumber(luaVM, options, "maxGenerations", 0.0));
    const unsigned char damageMask = ReadDamageMask(luaVM, options);
    CClientEntity* source = ReadTableElement(luaVM, options, "source");
    CClientEntity* target = ReadTableElement(luaVM, options, "target");

    CResource& resource = lua_getownerresource(luaVM);
    CClientDummy* fire = CStaticFunctionDefinitions::CreateElement(resource, "fire", "");
    if (!fire)
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    fire->SetPosition(vecPosition);
    SetNumber(fire, FireManager::KEY_DURATION, duration);
    SetNumber(fire, FireManager::KEY_EXPIRY, duration > 0.0 ? static_cast<double>(NowMs()) + duration : 0.0);
    SetNumber(fire, FireManager::KEY_STRENGTH, strength);
    SetBool(fire, FireManager::KEY_DAMAGE, damage);
    SetNumber(fire, FireManager::KEY_DAMAGE_MASK, damageMask);
    SetBool(fire, FireManager::KEY_SPREAD, spread);
    SetNumber(fire, FireManager::KEY_MAX_GENERATIONS, maxGenerations);
    SetNumber(fire, FireManager::KEY_GENERATION, 0.0);
    SetElement(fire, FireManager::KEY_SOURCE, source);
    SetElement(fire, FireManager::KEY_TARGET, target);

    if (resource.GetElementGroup())
        resource.GetElementGroup()->Add(fire);

    lua_pushelement(luaVM, fire);
    return 1;
}

int CLuaFireDefs::ExtinguishFire(lua_State* luaVM)
{
    if (lua_type(luaVM, 1) == LUA_TLIGHTUSERDATA)
    {
        CClientEntity* fire = lua_toelement(luaVM, 1);
        if (IsFire(fire) && CanMutate(fire))
        {
            lua_pushboolean(luaVM, CStaticFunctionDefinitions::DestroyElement(*fire));
            return 1;
        }
    }

    CScriptArgReader argStream(luaVM);
    if (argStream.NextIsNone())
    {
        lua_pushboolean(luaVM, CStaticFunctionDefinitions::ExtinguishAllFires());
        return 1;
    }

    CVector vecPosition;
    float fRadius;
    argStream.ReadVector3D(vecPosition);
    argStream.ReadNumber(fRadius, 1.0f);
    if (!argStream.HasErrors())
    {
        lua_pushboolean(luaVM, CStaticFunctionDefinitions::ExtinguishFireInRadius(vecPosition, fRadius));
        return 1;
    }

    m_pScriptDebugging->LogCustom(luaVM, argStream.GetFullErrorMessage());
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireDuration(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double value = 0.0;
    if (ReadFire(luaVM, fire) && GetNumber(fire, FireManager::KEY_DURATION, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDuration(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double duration = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(duration);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire) && duration >= 0.0)
    {
        SetNumber(fire, FireManager::KEY_DURATION, duration);
        SetNumber(fire, FireManager::KEY_EXPIRY, duration > 0.0 ? static_cast<double>(NowMs()) + duration : 0.0);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireRemainingTime(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double expiry = 0.0;
    if (ReadFire(luaVM, fire) && GetNumber(fire, FireManager::KEY_EXPIRY, expiry))
    {
        lua_pushnumber(luaVM, expiry <= 0.0 ? 0.0 : std::max(0.0, expiry - static_cast<double>(NowMs())));
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireRemainingTime(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double remaining = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(remaining);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire) && remaining >= 0.0)
    {
        SetNumber(fire, FireManager::KEY_EXPIRY, remaining > 0.0 ? static_cast<double>(NowMs()) + remaining : static_cast<double>(NowMs()));
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireStrength(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double value = 0.0;
    if (ReadFire(luaVM, fire) && GetNumber(fire, FireManager::KEY_STRENGTH, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireStrength(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double strength = 0.0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(strength);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire) && strength > 0.0)
    {
        SetNumber(fire, FireManager::KEY_STRENGTH, strength);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::IsFireDamageEnabled(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    bool value = false;
    if (ReadFire(luaVM, fire) && GetBool(fire, FireManager::KEY_DAMAGE, value))
        lua_pushboolean(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDamageEnabled(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    bool enabled = false;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadBool(enabled);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire))
    {
        SetBool(fire, FireManager::KEY_DAMAGE, enabled);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireDamageTargets(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double value = FireManager::DAMAGE_ALL;
    if (ReadFire(luaVM, fire))
    {
        GetNumber(fire, FireManager::KEY_DAMAGE_MASK, value);
        PushDamageMask(luaVM, static_cast<unsigned char>(static_cast<unsigned int>(value)) & FireManager::DAMAGE_ALL);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireDamageTargets(lua_State* luaVM)
{
    CClientEntity* fire = lua_toelement(luaVM, 1);
    if (!IsFire(fire) || !CanMutate(fire) || !lua_istable(luaVM, 2))
    {
        lua_pushboolean(luaVM, false);
        return 1;
    }

    unsigned char mask = 0;
    if (ReadTableBool(luaVM, 2, "players", true))
        mask |= FireManager::DAMAGE_PLAYERS;
    if (ReadTableBool(luaVM, 2, "peds", true))
        mask |= FireManager::DAMAGE_PEDS;
    if (ReadTableBool(luaVM, 2, "vehicles", true))
        mask |= FireManager::DAMAGE_VEHICLES;
    if (ReadTableBool(luaVM, 2, "objects", true))
        mask |= FireManager::DAMAGE_OBJECTS;
    SetNumber(fire, FireManager::KEY_DAMAGE_MASK, mask);
    lua_pushboolean(luaVM, true);
    return 1;
}

int CLuaFireDefs::IsFireSpreadEnabled(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    bool value = false;
    if (ReadFire(luaVM, fire) && GetBool(fire, FireManager::KEY_SPREAD, value))
        lua_pushboolean(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireSpreadEnabled(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    bool enabled = false;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadBool(enabled);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire))
    {
        SetBool(fire, FireManager::KEY_SPREAD, enabled);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireMaxGenerations(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    double value = 0.0;
    if (ReadFire(luaVM, fire) && GetNumber(fire, FireManager::KEY_MAX_GENERATIONS, value))
        lua_pushnumber(luaVM, value);
    else
        lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::SetFireMaxGenerations(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    unsigned int generations = 0;
    CScriptArgReader args(luaVM);
    args.ReadUserData(fire);
    args.ReadNumber(generations);
    if (!args.HasErrors() && IsFire(fire) && CanMutate(fire) && generations <= 255)
    {
        SetNumber(fire, FireManager::KEY_MAX_GENERATIONS, generations);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireSource(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    if (ReadFire(luaVM, fire))
    {
        CClientEntity* value = GetElement(fire, FireManager::KEY_SOURCE);
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
    CClientEntity* fire = lua_toelement(luaVM, 1);
    CClientEntity* value = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    if (IsFire(fire) && CanMutate(fire) && (lua_isnoneornil(luaVM, 2) || value))
    {
        SetElement(fire, FireManager::KEY_SOURCE, value);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}

int CLuaFireDefs::GetFireTarget(lua_State* luaVM)
{
    CClientEntity* fire = nullptr;
    if (ReadFire(luaVM, fire))
    {
        CClientEntity* value = GetElement(fire, FireManager::KEY_TARGET);
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
    CClientEntity* fire = lua_toelement(luaVM, 1);
    CClientEntity* value = lua_isnoneornil(luaVM, 2) ? nullptr : lua_toelement(luaVM, 2);
    if (IsFire(fire) && CanMutate(fire) && (lua_isnoneornil(luaVM, 2) || value))
    {
        SetElement(fire, FireManager::KEY_TARGET, value);
        lua_pushboolean(luaVM, true);
        return 1;
    }
    lua_pushboolean(luaVM, false);
    return 1;
}
